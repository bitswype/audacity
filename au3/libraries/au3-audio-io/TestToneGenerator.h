/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneGenerator.h

  bitswype fork: synthesises sine, pink, or white noise into a float
  buffer.  Used by the Channel Test Tone feature to verify physical
  device routing and the routing-matrix engine without needing
  recorded audio content.

  This class is intentionally tiny and standalone (no Audacity
  dependencies beyond <cstdint>) so it is unit-testable without
  spinning up the audio engine.

  Threading: single-threaded.  Configure() and Render() must be
  called from the same thread.  The audio callback owns its own
  instance; the dialog hands updated parameters to AudioIO via an
  atomic snapshot, and AudioIO calls Configure() on its instance
  inside the callback before the next Render().

**********************************************************************/
#pragma once

#include "au3-audio-devices/AudioIOBase.h" // for AUDIO_IO_API
#include "au3-mixer/PlaybackOutputMask.h"
#include <cstddef>
#include <cstdint>

class AUDIO_IO_API TestToneGenerator
{
public:
    enum class Type {
        Sine, //!< pure sine at frequencyHz; frequency >= 1 Hz
        Pink, //!< Voss-McCartney pink noise (1/f); frequency ignored
        White, //!< uniform white noise; frequency ignored
    };

    TestToneGenerator() = default;

    //! Configure tone parameters.  Phase / pink history are NOT reset
    //! by Configure -- callers wanting a glitch-free transition leave
    //! state alone; callers wanting a fresh start call Reset() too.
    //!
    //! @p sampleRate must be > 0.  @p frequencyHz is clamped to
    //! [0, sampleRate/2] for Sine; ignored for Pink/White.
    //! @p levelDb is in dBFS (0 = full-scale).  Values <= -200 are
    //! treated as silence.
    void Configure(Type type, double frequencyHz, double levelDb, double sampleRate);

    //! Reset internal state (sine phase, pink history, white RNG).
    //! Configure() does NOT call this -- continuous mode-switching keeps
    //! audio glitch-free.  Call before each new Play press if you want
    //! a deterministic starting state.
    void Reset();

    //! Replace @p numFrames samples in @p dest with synthesized tone.
    //! Output range is [-1, +1] times the configured linear amplitude.
    //! Must not be called before Configure().
    void Render(float* dest, std::size_t numFrames);

    //! Pure-function sample-rate-aware test helper: returns true iff
    //! the configured frequency exceeds the configured Nyquist.  The
    //! audio callback uses this to short-circuit to silence rather
    //! than emit aliased garbage when the user dials a sine above
    //! Nyquist.
    bool ExceedsNyquist() const;

private:
    //! Pink noise generator -- 7-row Voss-McCartney.  More rows trade
    //! CPU for spectrum accuracy at low frequencies; 7 is the standard
    //! Audacity-friendly choice for ~1/f down to a few Hz.
    float NextPinkSample();
    //! White noise sample in [-1, +1].
    float NextWhiteSample();
    //! Advance + read next sine sample.
    float NextSineSample();

    Type mType = Type::Sine;
    double mFrequencyHz = 1000.0;
    double mLinearAmp = 0.1;      //!< 10^(levelDb/20)
    double mSampleRate = 48000.0;
    double mPhase = 0.0;          //!< sine phase in radians [0, 2pi)
    double mPhaseInc = 0.0;       //!< 2*pi*freq/sampleRate

    // Pink noise state.
    uint32_t mPinkRows[7]{};
    uint32_t mPinkRunningSum = 0;
    uint32_t mPinkCount = 0;

    // xorshift64 RNG -- shared by white and pink (the dither rows draw
    // from the same stream).  Non-zero seed; ensures the very first
    // pull doesn't collapse to zero forever.
    uint64_t mRngState = 0x9E3779B97F4A7C15ull;
};

//! bitswype fork: parameters for one test-tone playback session.
//!
//! Two operating modes:
//!  - DirectHW: bypasses the routing matrix entirely.  Tone is written
//!    to outputBuffer[bit] for every bit set in @a mask whose index is
//!    less than the device's playback channel count.  Use to verify
//!    physical wiring ("speaker B is on output A").
//!  - ThroughMatrix: tone runs through the production
//!    RouteTrackSamples() pipeline using @a mask as the assignment.
//!    Use to verify the routing engine produces the expected outputs
//!    for a given mask.
//!
//! @a frequencyHz applies to Sine only (Pink/White ignore it).
//! @a levelDb is in dBFS.  Values <= -200 dBFS are treated as silence
//! (the stream stays open, but emits zeros).
struct AUDIO_IO_API TestToneRequest
{
    enum class Mode {
        Off,         //!< no tone (used as a null state)
        DirectHW,    //!< write tone directly to physical output channels
        ThroughMatrix, //!< route via RouteTrackSamples with @a mask
    };

    Mode mode = Mode::Off;
    TestToneGenerator::Type toneType = TestToneGenerator::Type::Sine;
    double frequencyHz = 1000.0;
    double levelDb = -20.0;
    //! In DirectHW: which physical outputs receive the tone.
    //! In ThroughMatrix: the routing assignment (passed verbatim to
    //! RouteTrackSamples).
    PlaybackOutputMask mask;
};
