/*
* Audacity: A Digital Audio Editor
*/
#include "playbackroutingmodel.h"

#include "au3-audio-devices/AudioIOBase.h"
#include "au3-mixer/PlaybackOutputMask.h"
#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/au3types.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/iau3project.h"
#include "trackedit/itrackeditproject.h"

#include "framework/global/log.h"

using namespace au::au3;

namespace {
constexpr int kMaxChannelCount = 128;
}

namespace au::playback {
PlaybackRoutingModel::PlaybackRoutingModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PlaybackRoutingModel::load()
{
    rebuildFromProject();

    if (auto project = globalContext()->currentTrackeditProject()) {
        project->tracksChanged().onReceive(this, [this](const std::vector<trackedit::Track>&) {
            rebuildFromProject();
        });
    }
}

int PlaybackRoutingModel::trackCount() const
{
    return static_cast<int>(m_rows.size());
}

int PlaybackRoutingModel::channelCount() const
{
    return m_channelCount;
}

int PlaybackRoutingModel::displayChannelCount() const
{
    int highestSet = m_channelCount;
    for (const auto& row : m_rows) {
        // Find the position of the highest set bit across the 128-bit mask.
        for (int bit = kMaxChannelCount - 1; bit >= highestSet; --bit) {
            const bool set = (bit < 64)
                ? ((row.lo & (uint64_t(1) << bit)) != 0)
                : ((row.hi & (uint64_t(1) << (bit - 64))) != 0);
            if (set) {
                highestSet = bit + 1;
                break;
            }
        }
    }
    return std::min(highestSet, kMaxChannelCount);
}

bool PlaybackRoutingModel::isDeviceChannel(int channel) const
{
    return channel >= 0 && channel < m_channelCount;
}

QString PlaybackRoutingModel::trackName(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_rows.size())) {
        return {};
    }
    return m_rows[trackIndex].name;
}

bool PlaybackRoutingModel::isRouted(int trackIndex, int channel) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_rows.size())) {
        return false;
    }
    if (channel < 0 || channel >= kMaxChannelCount) {
        return false;
    }
    const auto& row = m_rows[trackIndex];
    if (channel < 64) {
        return (row.lo & (uint64_t(1) << channel)) != 0;
    } else {
        return (row.hi & (uint64_t(1) << (channel - 64))) != 0;
    }
}

void PlaybackRoutingModel::setRouted(int trackIndex, int channel, bool on)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_rows.size())) {
        return;
    }
    if (channel < 0 || channel >= kMaxChannelCount) {
        return;
    }
    auto& row = m_rows[trackIndex];
    if (channel < 64) {
        const uint64_t bit = uint64_t(1) << channel;
        if (on) { row.lo |= bit; } else { row.lo &= ~bit; }
    } else {
        const uint64_t bit = uint64_t(1) << (channel - 64);
        if (on) { row.hi |= bit; } else { row.hi &= ~bit; }
    }
    emit cellChanged(trackIndex, channel);
}

void PlaybackRoutingModel::resetIdentity()
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        m_rows[i].lo = 0;
        m_rows[i].hi = 0;
        if (i < 64) {
            m_rows[i].lo = uint64_t(1) << i;
        } else if (i < 128) {
            m_rows[i].hi = uint64_t(1) << (i - 64);
        }
    }
    for (size_t i = 0; i < m_rows.size(); ++i) {
        for (int ch = 0; ch < m_channelCount; ++ch) {
            emit cellChanged(static_cast<int>(i), ch);
        }
    }
}

void PlaybackRoutingModel::clearAll()
{
    for (auto& row : m_rows) {
        row.lo = 0;
        row.hi = 0;
    }
    for (size_t i = 0; i < m_rows.size(); ++i) {
        for (int ch = 0; ch < m_channelCount; ++ch) {
            emit cellChanged(static_cast<int>(i), ch);
        }
    }
}

void PlaybackRoutingModel::apply()
{
    auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto au3Project = reinterpret_cast<Au3Project*>(project->au3ProjectPtr());
    if (!au3Project) {
        return;
    }

    for (const auto& row : m_rows) {
        Au3WaveTrack* waveTrack = au::au3::DomAccessor::findWaveTrack(
            *au3Project, Au3TrackId(row.trackId));
        if (waveTrack) {
            PlaybackOutputMask mask;
            mask.lo = row.lo;
            mask.hi = row.hi;
            waveTrack->SetPlaybackOutputMask(mask);
        }
    }
}

void PlaybackRoutingModel::rebuildFromProject()
{
    m_rows.clear();
    m_channelCount = std::max(1, AudioIOPlaybackChannels.ReadWithDefault(2));
    if (m_channelCount > kMaxChannelCount) {
        m_channelCount = kMaxChannelCount;
    }
    emit channelCountChanged();

    auto project = globalContext()->currentProject();
    auto trackedit = globalContext()->currentTrackeditProject();
    if (!project || !trackedit) {
        emit tracksChanged();
        return;
    }
    auto au3Project = reinterpret_cast<Au3Project*>(project->au3ProjectPtr());
    if (!au3Project) {
        emit tracksChanged();
        return;
    }

    for (const auto& track : trackedit->trackList()) {
        if (track.type == trackedit::TrackType::Label) {
            continue;
        }
        const Au3WaveTrack* waveTrack = au::au3::DomAccessor::findWaveTrack(
            *au3Project, Au3TrackId(track.id));
        if (!waveTrack) {
            continue;
        }
        Row row;
        row.trackId = static_cast<int64_t>(track.id);
        row.name = track.title.toQString();
        const auto mask = waveTrack->GetPlaybackOutputMask();
        row.lo = mask.lo;
        row.hi = mask.hi;
        m_rows.push_back(std::move(row));
    }

    emit tracksChanged();
}
}
