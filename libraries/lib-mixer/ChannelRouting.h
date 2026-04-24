/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.h

  Determines which output channel(s) each playback track routes to.
  Separates the routing decision from the AudioIO playback loop so it
  can be tested independently.

  As of the 128-bit static-mask refactor, every playback track carries
  an explicit PlaybackOutputMask.  Empty mask = silent.  There is no
  more "auto routing" state, and no sentinel bit -- the mask either
  has bits or it does not.  See
  .claude/plans/playback-routing-128bit-static-masks.md.

*******************************************************************/
#pragma once

#include "MixerOptions.h" // for MIXER_API
#include "PlaybackOutputMask.h"

#include <cstddef>
#include <vector>

//! Per-track output channel assignment.  In the static-mask model this
//! is a thin wrapper around the mask -- kept as a struct for forward
//! compatibility (future fields might carry per-track gain, etc).
struct TrackChannelAssignment
{
   //! 128-bit bitmask of output channels to route to.  Empty = silent.
   //! For multi-channel source tracks, source channels walk the set
   //! bits in low-to-high bit order (all of lo, then all of hi).
   PlaybackOutputMask outputMask{};
};

//! Snapshot per-track output mask assignments for a set of playback
//! tracks.  Essentially a 1:1 copy from @p trackOutputMasks, existing
//! as a function so callers have a single place to reason about
//! routing and so the function can evolve if the assignment model
//! grows more fields.
//!
//! @param trackOutputMasks Per-track masks (same order as the
//!                         playback sequence list).  Each mask is
//!                         used verbatim.
//! @return Per-track assignments, same length as @p trackOutputMasks.
MIXER_API std::vector<TrackChannelAssignment> ComputeChannelAssignments(
   const std::vector<PlaybackOutputMask>& trackOutputMasks);
