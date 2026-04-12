/**********************************************************************

  Audacity: A Digital Audio Editor

  MixOutput.h

  Extracted mixing logic from AudioIO::ProcessPlaybackSlices.
  Pure function that mixes per-track processing buffers into
  master output buffers according to channel assignments.

  Testable independently of AudioIO, PortAudio, ring buffers, or threads.

*******************************************************************/
#pragma once

#include "ChannelRouting.h"
#include <cstddef>
#include <functional>
#include <vector>

//! Per-track gain callback: given (trackIndex, channelIndex), return gain.
using GainFunction = std::function<float(size_t trackIndex, size_t channel)>;

//! Mix per-track processing buffers into master output buffers
//! according to channel assignments.
//!
//! @param processingBuffers  Flat list of per-channel buffers for all tracks
//!   (track 0 ch 0, track 0 ch 1, ..., track 1 ch 0, ...)
//! @param masterBuffers  Output: one buffer per output channel, samples are
//!   accumulated (added to existing content, not overwritten)
//! @param assignments  Per-track output channel assignments from
//!   ComputeChannelAssignments
//! @param trackChannelCounts  NChannels() per track
//! @param numOutputChannels  Number of output channels
//! @param numSamples  Number of samples to process per channel
//! @param gainFn  Callback to get per-channel gain for each track
void MixToOutputBuffers(
   const std::vector<std::vector<float>>& processingBuffers,
   std::vector<std::vector<float>>& masterBuffers,
   const std::vector<TrackChannelAssignment>& assignments,
   const std::vector<size_t>& trackChannelCounts,
   size_t numOutputChannels,
   size_t numSamples,
   const GainFunction& gainFn);
