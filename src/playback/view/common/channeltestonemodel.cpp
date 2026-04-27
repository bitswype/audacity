/*
* Audacity: A Digital Audio Editor
*/
#include "channeltestonemodel.h"

#include "au3-audio-devices/AudioIOBase.h"
#include "au3-audio-io/AudioIO.h"
#include "au3-mixer/PlaybackOutputMask.h"

#include "framework/global/log.h"

namespace {
constexpr int kMaxChannelCount = 128;
}

namespace au::playback {
ChannelTestToneModel::ChannelTestToneModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ChannelTestToneModel::load()
{
    m_channelCount = std::max(1, AudioIOPlaybackChannels.ReadWithDefault(2));
    if (m_channelCount > kMaxChannelCount) {
        m_channelCount = kMaxChannelCount;
    }
    m_active = AudioIO::Get() && AudioIO::Get()->IsTestToneActive();
    emit channelCountChanged();
    emit paramsChanged();
    emit activeChanged();
}

void ChannelTestToneModel::setMode(int v)
{
    if (m_mode == v) {
        return;
    }
    m_mode = v;
    emit paramsChanged();
    pushParamsToAudioIO();
}

void ChannelTestToneModel::setToneType(int v)
{
    if (m_toneType == v) {
        return;
    }
    m_toneType = v;
    emit paramsChanged();
    pushParamsToAudioIO();
}

void ChannelTestToneModel::setFrequencyHz(double v)
{
    if (m_frequencyHz == v) {
        return;
    }
    m_frequencyHz = v;
    emit paramsChanged();
    pushParamsToAudioIO();
}

void ChannelTestToneModel::setLevelDb(double v)
{
    if (m_levelDb == v) {
        return;
    }
    m_levelDb = v;
    emit paramsChanged();
    pushParamsToAudioIO();
}

bool ChannelTestToneModel::isChannelOn(int channel) const
{
    if (channel < 0 || channel >= kMaxChannelCount) {
        return false;
    }
    if (channel < 64) {
        return (m_maskLo & (uint64_t(1) << channel)) != 0;
    } else {
        return (m_maskHi & (uint64_t(1) << (channel - 64))) != 0;
    }
}

void ChannelTestToneModel::setChannelOn(int channel, bool on)
{
    if (channel < 0 || channel >= kMaxChannelCount) {
        return;
    }
    if (channel < 64) {
        const uint64_t bit = uint64_t(1) << channel;
        if (on) { m_maskLo |= bit; } else { m_maskLo &= ~bit; }
    } else {
        const uint64_t bit = uint64_t(1) << (channel - 64);
        if (on) { m_maskHi |= bit; } else { m_maskHi &= ~bit; }
    }
    emit channelStateChanged(channel);
    pushParamsToAudioIO();
}

void ChannelTestToneModel::selectAllChannels()
{
    m_maskLo = 0;
    m_maskHi = 0;
    for (int ch = 0; ch < m_channelCount; ++ch) {
        if (ch < 64) {
            m_maskLo |= uint64_t(1) << ch;
        } else {
            m_maskHi |= uint64_t(1) << (ch - 64);
        }
    }
    for (int ch = 0; ch < m_channelCount; ++ch) {
        emit channelStateChanged(ch);
    }
    pushParamsToAudioIO();
}

void ChannelTestToneModel::clearAllChannels()
{
    m_maskLo = 0;
    m_maskHi = 0;
    for (int ch = 0; ch < m_channelCount; ++ch) {
        emit channelStateChanged(ch);
    }
    pushParamsToAudioIO();
}

void ChannelTestToneModel::start()
{
    auto* io = AudioIO::Get();
    if (!io) {
        return;
    }

    TestToneRequest request;
    request.mode = (m_mode == 1) ? TestToneRequest::Mode::DirectHW
                                 : (m_mode == 2) ? TestToneRequest::Mode::ThroughMatrix
                                 : TestToneRequest::Mode::Off;
    request.toneType = static_cast<TestToneGenerator::Type>(m_toneType);
    request.frequencyHz = m_frequencyHz;
    request.levelDb = m_levelDb;
    request.mask.lo = m_maskLo;
    request.mask.hi = m_maskHi;

    AudioIOStartStreamOptions options;
    options.rate = 44100.0;
    if (io->StartTestTone(request, options)) {
        m_active = true;
        emit activeChanged();
    }
}

void ChannelTestToneModel::stop()
{
    auto* io = AudioIO::Get();
    if (!io) {
        return;
    }
    io->StopTestTone();
    m_active = false;
    emit activeChanged();
}

void ChannelTestToneModel::pushParamsToAudioIO()
{
    auto* io = AudioIO::Get();
    if (!io || !io->IsTestToneActive()) {
        return;
    }
    TestToneRequest request;
    request.mode = (m_mode == 1) ? TestToneRequest::Mode::DirectHW
                                 : (m_mode == 2) ? TestToneRequest::Mode::ThroughMatrix
                                 : TestToneRequest::Mode::Off;
    request.toneType = static_cast<TestToneGenerator::Type>(m_toneType);
    request.frequencyHz = m_frequencyHz;
    request.levelDb = m_levelDb;
    request.mask.lo = m_maskLo;
    request.mask.hi = m_maskHi;
    io->UpdateTestTone(request);
}
}
