/**********************************************************************

  Audacity: A Digital Audio Editor

  MixOutput.cpp

*******************************************************************/

#include "MixOutput.h"
#include <algorithm>
#include <cassert>

void MixToOutputBuffers(
   const std::vector<std::vector<float>>& processingBuffers,
   std::vector<std::vector<float>>& masterBuffers,
   const std::vector<TrackChannelAssignment>& assignments,
   const std::vector<size_t>& trackChannelCounts,
   size_t numOutputChannels,
   size_t numSamples,
   const GainFunction& gainFn)
{
   assert(assignments.size() == trackChannelCounts.size());
   assert(masterBuffers.size() >= numOutputChannels);

   size_t bufferIndex = 0;

   for (size_t trackIdx = 0; trackIdx < trackChannelCounts.size(); ++trackIdx) {
      const auto numChannels = trackChannelCounts[trackIdx];
      const int assignedOutput =
         (trackIdx < assignments.size())
         ? assignments[trackIdx].outputChannel
         : -1;

      if (numChannels == 0) {
         // Skip zero-channel tracks
         continue;
      }

      if (assignedOutput >= 0 && numChannels > 1) {
         // Case 1: Multi-channel track with assigned output
         const auto startCh = static_cast<size_t>(assignedOutput);
         const auto cnt = std::min(numChannels, numOutputChannels - startCh);

         for (size_t n = 0; n < cnt; ++n) {
            const float volume = gainFn(trackIdx, n);
            const auto& src = processingBuffers[bufferIndex + n];
            auto& dst = masterBuffers[startCh + n];

            for (size_t i = 0; i < numSamples; ++i)
               dst[i] += src[i] * volume;
         }

      } else if (numChannels > 1 && assignedOutput < 0) {
         // Case 2: Multi-channel track, legacy routing
         const auto cnt = std::min(numChannels, numOutputChannels);

         for (size_t n = 0; n < cnt; ++n) {
            const float volume = gainFn(trackIdx, n);
            const auto& src = processingBuffers[bufferIndex + n];
            auto& dst = masterBuffers[n];

            for (size_t i = 0; i < numSamples; ++i)
               dst[i] += src[i] * volume;
         }

      } else if (numChannels == 1 && assignedOutput >= 0) {
         // Case 3: Mono track with assigned output channel
         const auto targetCh = static_cast<size_t>(assignedOutput);
         const float volume = gainFn(trackIdx, 0);
         const auto& src = processingBuffers[bufferIndex];
         auto& dst = masterBuffers[targetCh];

         for (size_t i = 0; i < numSamples; ++i)
            dst[i] += src[i] * volume;

      } else if (numChannels == 1) {
         // Case 4: Mono track, legacy -- duplicate to all outputs with per-channel gain
         const auto& src = processingBuffers[bufferIndex];

         for (size_t n = 0; n < numOutputChannels; ++n) {
            const float volume = gainFn(trackIdx, n);
            auto& dst = masterBuffers[n];

            for (size_t i = 0; i < numSamples; ++i)
               dst[i] += src[i] * volume;
         }
      }

      bufferIndex += numChannels;
   }
}
