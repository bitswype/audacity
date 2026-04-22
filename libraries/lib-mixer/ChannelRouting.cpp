/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.cpp

*******************************************************************/

#include "ChannelRouting.h"

std::vector<TrackChannelAssignment> ComputeChannelAssignments(
   const std::vector<size_t>& trackChannelCounts,
   const std::vector<uint64_t>& trackOutputMasks,
   size_t numOutputChannels)
{
   std::vector<TrackChannelAssignment> result(trackChannelCounts.size());

   // Short-circuit copy of explicit masks.  A track with a non-zero mask
   // bypasses all the identity-routing / overflow logic below -- the
   // caller (AudioIO) applies the mask directly to route samples.
   // The per-track outputMask is still set even when numOutputChannels<=2
   // so that an explicitly-routed track can still reach the few outputs
   // that exist.
   for (size_t i = 0; i < result.size(); ++i) {
      if (i < trackOutputMasks.size() && trackOutputMasks[i] != 0)
         result[i].outputMask = trackOutputMasks[i];
   }

   // For stereo or mono output, tracks without an explicit mask use
   // legacy behavior.
   if (numOutputChannels <= 2) {
      for (auto& a : result)
         if (a.outputMask == 0)
            a.outputChannel = -1;
      return result;
   }

   // Multi-channel output: assign tracks WITHOUT an explicit mask to
   // output channels sequentially. Once any such track doesn't fit, all
   // remaining auto-routed tracks get legacy behavior to avoid mixing
   // collisions (a legacy track duplicates to all outputs, which would
   // collide with identity-routed tracks on the same channels).
   // Explicitly masked tracks are skipped in the "next channel" counter
   // because the user picked their channels manually.
   size_t nextOutputChannel = 0;
   bool overflow = false;

   for (size_t i = 0; i < trackChannelCounts.size(); ++i) {
      if (result[i].outputMask != 0)
         // User-specified routing; leave alone.
         continue;

      const auto trackChannels = trackChannelCounts[i];

      if (!overflow &&
          trackChannels > 0 &&
          nextOutputChannel + trackChannels <= numOutputChannels)
      {
         result[i].outputChannel = static_cast<int>(nextOutputChannel);
         nextOutputChannel += trackChannels;
      } else {
         // Track doesn't fit (or previous track overflowed):
         // fall back to legacy behavior for this and all remaining tracks
         result[i].outputChannel = -1;
         overflow = true;
      }
   }

   return result;
}
