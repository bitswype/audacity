/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QStringList>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace au::playback {
//! View model for the Playback Routing Matrix dialog.
//!
//! Exposes the project's wave tracks and the device output channel
//! count to QML, plus a per-cell read/write API for the checkbox
//! grid.  The user's edits are buffered in @c m_pendingMasks and
//! committed to the underlying WaveTracks (with an undo entry) by
//! @c apply().  @c reset() restores identity routing for every track.
//! @c clear() sets every track silent.
class PlaybackRoutingModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY channelCountChanged FINAL)
    //! Number of columns the dialog should display: the device channel
    //! count expanded to include any track's highest set bit (capped at
    //! 128).  Lets the user see and clear bits beyond the current device.
    Q_PROPERTY(int displayChannelCount READ displayChannelCount NOTIFY channelCountChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit PlaybackRoutingModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    int trackCount() const;
    int channelCount() const;
    int displayChannelCount() const;

    Q_INVOKABLE QString trackName(int trackIndex) const;
    Q_INVOKABLE bool isRouted(int trackIndex, int channel) const;
    Q_INVOKABLE void setRouted(int trackIndex, int channel, bool on);
    //! True iff @p channel is within the active device channel count.
    //! False = off-device, displayed with an asterisk by the dialog.
    Q_INVOKABLE bool isDeviceChannel(int channel) const;

    Q_INVOKABLE void resetIdentity();
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void apply();

signals:
    void tracksChanged();
    void channelCountChanged();
    void cellChanged(int trackIndex, int channel);

private:
    void rebuildFromProject();

    struct Row {
        int64_t trackId = 0;
        QString name;
        // 128-bit pending mask, split into two uint64_t words.
        uint64_t lo = 0;
        uint64_t hi = 0;
    };
    std::vector<Row> m_rows;
    int m_channelCount = 2;
};
}
