/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.cpp

*******************************************************************/

#include "ChannelRouting.h"

std::vector<TrackChannelAssignment> ComputeChannelAssignments(
   const std::vector<size_t>& trackChannelCounts,
   size_t numOutputChannels)
{
   std::vector<TrackChannelAssignment> result(trackChannelCounts.size());

   // For stereo or mono output, use legacy behavior for all tracks
   if (numOutputChannels <= 2) {
      for (auto& a : result)
         a.outputChannel = -1;
      return result;
   }

   // Multi-channel output: assign tracks to output channels sequentially.
   // Once any track doesn't fit, all remaining tracks get legacy behavior
   // to avoid mixing collisions (a legacy track duplicates to all outputs,
   // which would collide with identity-routed tracks on the same channels).
   size_t nextOutputChannel = 0;
   bool overflow = false;

   for (size_t i = 0; i < trackChannelCounts.size(); ++i) {
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
