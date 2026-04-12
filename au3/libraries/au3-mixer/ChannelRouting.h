/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.h

  Determines which output channel(s) each playback track routes to.
  Separates the routing decision from the AudioIO playback loop so it
  can be tested independently.

*******************************************************************/
#pragma once

#include <cstddef>
#include <vector>

//! Per-track output channel assignment for multi-channel playback.
//! outputChannel == -1 means "use legacy stereo behavior" (mono
//! duplication with panning).
struct TrackChannelAssignment
{
   //! First output channel this track routes to.
   //! For mono tracks: the single output channel.
   //! For multi-channel tracks: the starting channel (identity mapping).
   //! -1 = legacy stereo behavior (duplicate mono to all outputs).
   int outputChannel = -1;
};

//! Compute output channel assignments for a set of playback tracks.
//!
//! @param trackChannelCounts NChannels() for each track in playback order
//! @param numOutputChannels  Number of output channels on the device
//! @return Per-track assignments, same size as trackChannelCounts
//!
//! Rules:
//! - If numOutputChannels <= 2: all tracks get assignment -1 (legacy stereo)
//! - If numOutputChannels > 2 and a mono track's position maps to a valid
//!   output channel: identity routing (track index -> output channel)
//! - Multi-channel tracks (NChannels > 1): identity from their buffer offset
//! - Tracks beyond the output channel count: legacy behavior (-1)
std::vector<TrackChannelAssignment> ComputeChannelAssignments(
   const std::vector<size_t>& trackChannelCounts,
   size_t numOutputChannels);
