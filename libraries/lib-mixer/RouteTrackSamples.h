/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamples.h

  Distributes one track's processing-buffer samples into the device
  master output buffers according to its TrackChannelAssignment.
  Extracted from AudioIO::ProcessPlaybackSlices so the routing logic
  can be unit-tested in isolation.

*******************************************************************/
#pragma once

#include "ChannelRouting.h"
#include "MixerOptions.h"

#include <cstddef>
#include <functional>

//! Route one track's samples into the device's master output buffers.
//!
//! Five cases, matching the original AudioIO::ProcessPlaybackSlices
//! logic exactly:
//!
//!   1. @c outputMask != 0: iterate set bits of the mask. For mono
//!      sources, every set bit receives a copy. For multi-channel
//!      sources, source channels walk through the set bits in order;
//!      if the mask has more bits than channels, the extra bits
//!      receive the last source channel (clamped).
//!   2. @c outputMask == 0, @c outputChannel >= 0, numSourceChannels > 1:
//!      identity routing starting at outputChannel. Master buffers at
//!      startCh+0..startCh+cnt-1 receive source channels 0..cnt-1,
//!      where cnt is clamped so it doesn't overrun numOutputChannels.
//!   3. @c outputMask == 0, @c outputChannel < 0, numSourceChannels > 1:
//!      legacy multi-channel routing. Source channel n goes to
//!      master n, clamped by numOutputChannels.
//!   4. @c outputMask == 0, @c outputChannel >= 0, numSourceChannels == 1:
//!      mono with assigned channel. Source goes to master[outputChannel].
//!   5. @c outputMask == 0, @c outputChannel < 0, numSourceChannels == 1:
//!      mono legacy. Source is duplicated to every master output, each
//!      multiplied by @c getChannelVolume(outputChannelIndex).
//!
//! Master buffers are written with += (accumulation), matching the
//! multi-track summing behavior. The caller is responsible for
//! zeroing master buffers before the first track of a callback.
//!
//! Side effect: cases 2 and 3 multiply @c processingBuffers in place
//! by the per-channel volume. This preserves the exact original
//! behavior. The other cases read without modification.
//!
//! @param assignment            Routing assignment for this track.
//! @param numSourceChannels     NChannels() of the source sequence.
//! @param numOutputChannels     Device playback channel count.
//! @param samplesAvailable      Samples to process per channel.
//! @param getChannelVolume      Maps to seq->GetChannelVolume(n) at
//!                              the call site. Called per output
//!                              channel in case 5, per source channel
//!                              elsewhere, at most O(channels) times.
//! @param processingBuffers     Array of numSourceChannels float*
//!                              pointing to at least samplesAvailable
//!                              floats each. Cases 2 and 3 mutate.
//! @param masterBuffers         Array of numOutputChannels float*
//!                              pointing to at least samplesAvailable
//!                              floats each. All cases accumulate.
MIXER_API void RouteTrackSamples(
   const TrackChannelAssignment& assignment,
   size_t numSourceChannels,
   size_t numOutputChannels,
   size_t samplesAvailable,
   const std::function<float(int)>& getChannelVolume,
   float* const* processingBuffers,
   float* const* masterBuffers);
