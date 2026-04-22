/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.h

  Determines which output channel(s) each playback track routes to.
  Separates the routing decision from the AudioIO playback loop so it
  can be tested independently.

*******************************************************************/
#pragma once

#include "MixerOptions.h" // for MIXER_API

#include <cstddef>
#include <cstdint>
#include <vector>

//! Per-track output channel assignment for multi-channel playback.
//!
//! Two ways a track can be routed:
//! - @c outputMask != 0: explicit bitmask, bit N = play on output N
//!   (set by the user via the Playback Routing dialog). Takes priority.
//! - @c outputMask == 0: fall back to @c outputChannel. -1 means
//!   "legacy stereo behavior" (mono duplication across all outputs);
//!   >=0 means "identity routing starting at this output channel".
struct TrackChannelAssignment
{
   //! First output channel this track routes to when @c outputMask is 0.
   //! For mono tracks: the single output channel.
   //! For multi-channel tracks: the starting channel (identity mapping).
   //! -1 = legacy stereo behavior (duplicate mono to all outputs).
   int outputChannel = -1;

   //! Bitmask of output channels to route to. Bit N = use output N.
   //! 0 = unset; fall back to @c outputChannel.
   //! For multi-channel source tracks, set bits are consumed
   //! sequentially: source channel 0 goes to the lowest set bit,
   //! source channel 1 to the next set bit, etc.
   uint64_t outputMask = 0;
};

//! Compute output channel assignments for a set of playback tracks.
//!
//! @param trackChannelCounts NChannels() for each track in playback order
//! @param trackOutputMasks   Explicit per-track output masks (bitmask of
//!                           output channels). Same size as
//!                           trackChannelCounts. Value 0 means the track
//!                           has no explicit routing, so identity rules
//!                           below apply.
//! @param numOutputChannels  Number of output channels on the device
//! @return Per-track assignments, same size as trackChannelCounts
//!
//! Rules (applied only to tracks whose outputMask is 0; tracks with a
//! non-zero mask pass through unchanged):
//! - If numOutputChannels <= 2: all tracks get assignment -1 (legacy stereo)
//! - If numOutputChannels > 2 and a mono track's position maps to a valid
//!   output channel: identity routing (track index -> output channel)
//! - Multi-channel tracks (NChannels > 1): identity from their buffer offset
//! - Tracks beyond the output channel count: legacy behavior (-1)
MIXER_API std::vector<TrackChannelAssignment> ComputeChannelAssignments(
   const std::vector<size_t>& trackChannelCounts,
   const std::vector<uint64_t>& trackOutputMasks,
   size_t numOutputChannels);
