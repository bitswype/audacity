/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamples.cpp

*******************************************************************/
#include "RouteTrackSamples.h"

#include <algorithm>
#include <cstdint>

void RouteTrackSamples(
   const TrackChannelAssignment& assignment,
   size_t numSourceChannels,
   size_t numOutputChannels,
   size_t samplesAvailable,
   const std::function<float(int)>& getChannelVolume,
   float* const* processingBuffers,
   float* const* masterBuffers)
{
   const uint64_t outputMask = assignment.outputMask;
   const int assignedOutput = assignment.outputChannel;
   const size_t numChannels = numSourceChannels;

   if (outputMask != 0) {
      // Case 1: mask-driven routing.
      unsigned srcChannel = 0;
      for (unsigned outCh = 0; outCh < numOutputChannels; ++outCh) {
         if (!(outputMask & (uint64_t(1) << outCh)))
            continue;
         const unsigned srcToUse = (numChannels > 1)
            ? std::min<unsigned>(srcChannel,
               static_cast<unsigned>(numChannels) - 1u)
            : 0u;
         const float volume = getChannelVolume(static_cast<int>(srcToUse));
         for (size_t i = 0; i < samplesAvailable; ++i)
            masterBuffers[outCh][i] +=
               processingBuffers[srcToUse][i] * volume;
         if (numChannels > 1)
            ++srcChannel;
      }
   } else if (assignedOutput >= 0 && numChannels > 1) {
      // Case 2: multi-channel with assigned output (identity).
      const auto startCh = static_cast<unsigned>(assignedOutput);
      const auto cnt = std::min(
         numChannels,
         static_cast<size_t>(numOutputChannels - startCh));
      for (unsigned n = 0; n < cnt; ++n) {
         const float volume = getChannelVolume(static_cast<int>(n));
         for (size_t i = 0; i < samplesAvailable; ++i) {
            processingBuffers[n][i] *= volume;
            masterBuffers[startCh + n][i] += processingBuffers[n][i];
         }
      }
   } else if (numChannels > 1 && assignedOutput < 0) {
      // Case 3: multi-channel, legacy routing.
      const auto cnt = std::min(numChannels, numOutputChannels);
      for (unsigned n = 0; n < cnt; ++n) {
         const float volume = getChannelVolume(static_cast<int>(n));
         for (size_t i = 0; i < samplesAvailable; ++i) {
            processingBuffers[n][i] *= volume;
            masterBuffers[n][i] += processingBuffers[n][i];
         }
      }
   } else if (numChannels == 1 && assignedOutput >= 0) {
      // Case 4: mono with assigned output.
      const auto targetChannel = static_cast<unsigned>(assignedOutput);
      const float volume = getChannelVolume(0);
      for (size_t i = 0; i < samplesAvailable; ++i)
         masterBuffers[targetChannel][i] +=
            processingBuffers[0][i] * volume;
   } else if (numChannels == 1) {
      // Case 5: mono, legacy stereo (duplicate to all outputs).
      for (unsigned n = 0; n < numOutputChannels; ++n) {
         const float volume = getChannelVolume(static_cast<int>(n));
         for (size_t i = 0; i < samplesAvailable; ++i)
            masterBuffers[n][i] +=
               processingBuffers[0][i] * volume;
      }
   }
}
