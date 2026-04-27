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
//! View model for the Recording Routing Matrix dialog.  Parallel
//! structure to PlaybackRoutingModel: rows are wave tracks, columns
//! are device input channels; each cell is "device input N feeds
//! this track".  See PlaybackRoutingModel for design notes.
class RecordingRoutingModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY channelCountChanged FINAL)
    Q_PROPERTY(int displayChannelCount READ displayChannelCount NOTIFY channelCountChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit RecordingRoutingModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    int trackCount() const;
    int channelCount() const;
    int displayChannelCount() const;

    Q_INVOKABLE QString trackName(int trackIndex) const;
    Q_INVOKABLE bool isRouted(int trackIndex, int channel) const;
    Q_INVOKABLE void setRouted(int trackIndex, int channel, bool on);
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
        uint64_t lo = 0;
        uint64_t hi = 0;
    };
    std::vector<Row> m_rows;
    int m_channelCount = 2;
};
}
