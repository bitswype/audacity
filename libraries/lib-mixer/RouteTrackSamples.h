/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamples.h

  Distributes one track's processing-buffer samples into the device
  master output buffers according to its TrackChannelAssignment.
  Extracted from AudioIO::ProcessPlaybackSlices so the routing logic
  can be unit-tested in isolation.

  Three cases after the 128-bit static-mask refactor:
    1. Empty mask: track is silent; master buffers untouched.
    2. Mono source (numSourceChannels == 1): the source is mixed into
       every bit position that is both set in the mask AND within the
       device's output channel count.
    3. Multi-channel source: source channels walk the set bits in
       low-to-high bit order (lo word first, then hi word).  Source
       channels beyond the set-bit count are dropped.  Bits past
       numOutputChannels are ignored (out-of-range for this device).

*******************************************************************/
#pragma once

#include "ChannelRouting.h"
#include "MixerOptions.h"

#include <cstddef>
#include <functional>

//! Route one track's samples into the device's master output buffers.
//!
//! Master buffers are accumulated with += (multi-track summing).  The
//! caller zeros master buffers before the first track of a callback.
//!
//! Side effect: when routing a multi-channel source, @p
//! processingBuffers are multiplied in place by the per-channel volume
//! before accumulation.  This preserves the original behavior (fewer
//! passes over the data at the cost of the mutation).  Mono and empty
//! cases do not mutate the source buffers.
//!
//! @param assignment            Routing assignment for this track.
//! @param numSourceChannels     NChannels() of the source sequence.
//! @param numOutputChannels     Device playback channel count.
//! @param samplesAvailable      Samples to process per channel.
//! @param getChannelVolume      Maps to seq->GetChannelVolume(n) at
//!                              the call site.  Called O(channels)
//!                              times.
//! @param processingBuffers     Array of numSourceChannels float*
//!                              pointing to at least samplesAvailable
//!                              floats each.  Mutated in the
//!                              multi-channel case.
//! @param masterBuffers         Array of numOutputChannels float*
//!                              pointing to at least samplesAvailable
//!                              floats each.  Accumulated.
MIXER_API void RouteTrackSamples(
   const TrackChannelAssignment& assignment,
   size_t numSourceChannels,
   size_t numOutputChannels,
   size_t samplesAvailable,
   const std::function<float(int)>& getChannelVolume,
   float* const* processingBuffers,
   float* const* masterBuffers);
