/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace au::playback {
//! View model for the Channel Test Tone dialog.
//!
//! Wraps AudioIO's StartTestTone / UpdateTestTone / StopTestTone with
//! Q_PROPERTY-friendly types.  Channel selection is exposed as a
//! per-bit checkbox grid so the same UI pattern as the routing
//! matrices works.
class ChannelTestToneModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    //! Mode: 0 = Off, 1 = Direct hardware, 2 = Through routing matrix.
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY paramsChanged FINAL)
    //! Tone type: 0 = Sine, 1 = Pink, 2 = White.
    Q_PROPERTY(int toneType READ toneType WRITE setToneType NOTIFY paramsChanged FINAL)
    Q_PROPERTY(double frequencyHz READ frequencyHz WRITE setFrequencyHz NOTIFY paramsChanged FINAL)
    Q_PROPERTY(double levelDb READ levelDb WRITE setLevelDb NOTIFY paramsChanged FINAL)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY channelCountChanged FINAL)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit ChannelTestToneModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    int mode() const { return m_mode; }
    void setMode(int v);
    int toneType() const { return m_toneType; }
    void setToneType(int v);
    double frequencyHz() const { return m_frequencyHz; }
    void setFrequencyHz(double v);
    double levelDb() const { return m_levelDb; }
    void setLevelDb(double v);
    int channelCount() const { return m_channelCount; }
    bool isActive() const { return m_active; }

    Q_INVOKABLE bool isChannelOn(int channel) const;
    Q_INVOKABLE void setChannelOn(int channel, bool on);
    Q_INVOKABLE void selectAllChannels();
    Q_INVOKABLE void clearAllChannels();

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void paramsChanged();
    void channelCountChanged();
    void channelStateChanged(int channel);
    void activeChanged();

private:
    void pushParamsToAudioIO();

    int m_mode = 1;        // DirectHW
    int m_toneType = 0;    // Sine
    double m_frequencyHz = 1000.0;
    double m_levelDb = -20.0;
    uint64_t m_maskLo = 0;
    uint64_t m_maskHi = 0;
    int m_channelCount = 2;
    bool m_active = false;
};
}
