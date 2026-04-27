/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteRecordingSamples.h

  Bitswype fork: dual of RouteTrackSamples for the recording side.

  Given a track's PlaybackInputMask, the staged samples for each
  device input channel, and the destination track channel index, fill
  a destination buffer with the appropriate samples following these
  rules (the duals of the playback rules):

    - Empty mask: destination filled with zeros (track is not a
      recording target -- caller would normally not even invoke us
      in that case).
    - Mono target (numTrackChannels == 1): all in-range set bits SUM
      into the single destination buffer.
    - Multi target (numTrackChannels >= 2): walk set bits in low-to-
      high order; the @p trackChannel -th set bit drives this
      destination 1:1.  If the mask has fewer set bits than the track
      has channels, channels beyond the mask's popcount get silence.
      Extras beyond numTrackChannels are dropped (callers should not
      call this with trackChannel >= numTrackChannels).

  Bits beyond the device width are ignored.  Callers are expected to
  size the staging-buffers array to numDeviceChannels.

*******************************************************************/
#pragma once

#include "MixerOptions.h"
#include "PlaybackInputMask.h"

#include <cstddef>

//! Compute one destination buffer for a single (track, trackChannel)
//! recording destination.  See file-level doc for routing rules.
//!
//! @p stagingBuffers must be of size >= numDeviceChannels; each
//! element points to at least @p samplesAvailable floats already
//! drained (and possibly resampled) from the corresponding device
//! input ring buffer.  Bits in @p mask that exceed @p numDeviceChannels
//! are skipped.
//!
//! @p destBuffer must point to >= @p samplesAvailable floats.  This
//! function overwrites destBuffer in full (initialises before summing).
MIXER_API
void RouteRecordingSamples(
    const PlaybackInputMask& mask, size_t numTrackChannels, size_t trackChannel, size_t numDeviceChannels, size_t samplesAvailable,
    const float* const* stagingBuffers, float* destBuffer);
