/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneRender.h

  bitswype fork: pure-function rendering of one block of channel
  test tone into an interleaved output buffer.  Extracted from
  AudioIO::FillTestToneOutputBuffer so the dispatch logic can be
  unit-tested in isolation -- without standing up a PortAudio
  stream, an AudioIO singleton, or any GUI.

  This file is the testable seam: AudioIO's FillTestToneOutputBuffer
  remains responsible for the atomic-snapshot read, the meter-buffer
  mirror, and the output clamp.  Synthesis + per-mode distribution
  live here.

**********************************************************************/
#pragma once

#include "au3-audio-devices/AudioIOBase.h"        // for AUDIO_IO_API
#include "au3-mixer/PlaybackOutputMask.h"
#include "TestToneGenerator.h"  // for TestToneRequest::Mode + generator

#include <cstddef>
#include <vector>

//! Rendering parameters that the caller has already snapshotted.
//!
//! @a mode and @a mask are read from AudioIO's atomic state in the
//! production caller; tests pass them in literally.
//!
//! @a numPlaybackChannels is the number of channels the routing
//! engine treats as reachable.  Bits in @a mask at indices >=
//! numPlaybackChannels are silently ignored in DirectHW mode and
//! handled by RouteTrackSamples in ThroughMatrix mode (which clamps
//! identically).
//!
//! @a devicePlaybackChannels is the interleaved stride for the
//! output buffer.  When the device's native channel count exceeds
//! the user-configured playback channel count (e.g. ALSA opens the
//! device wide to defeat its plugin's channel-adaption logic),
//! devicePlaybackChannels >= numPlaybackChannels and the gap
//! between them carries silence.
struct AUDIO_IO_API TestToneRenderParams
{
    TestToneRequest::Mode mode = TestToneRequest::Mode::Off;
    PlaybackOutputMask mask;
    std::size_t numPlaybackChannels = 0;
    std::size_t devicePlaybackChannels = 0;
};

//! Render one block of test tone into @p outputInterleaved.
//!
//! Behaviour:
//!  - mode == Off or mask empty or numPlaybackChannels == 0: no-op,
//!    returns false without touching the output buffer.
//!  - mode == DirectHW: tone written additively to outputInterleaved
//!    at every set bit of @p params.mask whose index is less than
//!    numPlaybackChannels.  Bypasses the routing engine.
//!  - mode == ThroughMatrix: tone is run through the production
//!    RouteTrackSamples() with @p params.mask as the routing
//!    assignment, then the resulting deinterleaved buffers are
//!    interleaved into outputInterleaved.  Exercises the real
//!    routing engine -- divergence between modes for the same
//!    mask localises a bug to RouteTrackSamples.
//!
//! Scratch buffers are passed in so the caller owns their lifetime
//! and can pre-size them away from the audio callback.  This
//! function does NOT allocate provided the scratch is already
//! sized for >= framesPerBuffer / >= numPlaybackChannels (it will
//! call vector::resize() if the caller under-sized them, which is
//! a tech-debt fall-back; production callers in AudioIO pre-size
//! to a generous worst case in StartTestTone).
//!
//! @param outputInterleaved Interleaved buffer of length
//!        framesPerBuffer * devicePlaybackChannels.  Tone is added
//!        with += semantics so the caller can mix with playthrough
//!        or overlay other signals before calling.
//! @return true if any samples were written; false if the call was
//!         a no-op (off / empty mask / zero channels).
AUDIO_IO_API bool RenderTestToneInterleaved(
    const TestToneRenderParams& params, TestToneGenerator& gen, std::size_t framesPerBuffer, float* outputInterleaved,
    std::vector<float>& srcScratch, std::vector<std::vector<float> >& outScratch, std::vector<float*>& dstScratch);
