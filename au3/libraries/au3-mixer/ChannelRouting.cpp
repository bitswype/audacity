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

   // Multi-channel output: assign tracks to output channels sequentially
   size_t nextOutputChannel = 0;

   for (size_t i = 0; i < trackChannelCounts.size(); ++i) {
      const auto trackChannels = trackChannelCounts[i];

      // Check if this track fits in the remaining output channels
      if (nextOutputChannel + trackChannels <= numOutputChannels) {
         result[i].outputChannel = static_cast<int>(nextOutputChannel);
         nextOutputChannel += trackChannels;
      } else {
         // Track doesn't fit: fall back to legacy behavior
         result[i].outputChannel = -1;
      }
   }

   return result;
}
