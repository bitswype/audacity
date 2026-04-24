/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamples.cpp

*******************************************************************/
#include "RouteTrackSamples.h"

#include <algorithm>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
   #define AU_CTZ64(x) static_cast<unsigned>(__builtin_ctzll(x))
#else
   #include <intrin.h>
   static inline unsigned AU_CTZ64(uint64_t x)
   {
      unsigned long idx;
      _BitScanForward64(&idx, x);
      return static_cast<unsigned>(idx);
   }
#endif

namespace
{
//! Call @p visit(bit) for each set bit in the mask in low-to-high bit
//! order (0..63 from lo, then 64..127 from hi), stopping at bits that
//! would be out of range for the current device.
template <typename F>
void ForEachSetBitInRange(
   const PlaybackOutputMask& mask, size_t numOutputChannels, F&& visit)
{
   const unsigned cap = static_cast<unsigned>(
      std::min<size_t>(numOutputChannels, kPlaybackOutputMaskBits));
   // Low word: bits 0..63
   uint64_t w = mask.lo;
   while (w != 0) {
      const unsigned b = AU_CTZ64(w);
      if (b >= cap)
         return;
      visit(b);
      w &= w - 1; // clear lowest set bit
   }
   // High word: bits 64..127
   w = mask.hi;
   while (w != 0) {
      const unsigned b = 64u + AU_CTZ64(w);
      if (b >= cap)
         return;
      visit(b);
      w &= w - 1;
   }
}
} // namespace

void RouteTrackSamples(
   const TrackChannelAssignment& assignment,
   size_t numSourceChannels,
   size_t numOutputChannels,
   size_t samplesAvailable,
   const std::function<float(int)>& getChannelVolume,
   float* const* processingBuffers,
   float* const* masterBuffers)
{
   const auto& mask = assignment.outputMask;

   // Case 1: empty mask -> silent.
   if (mask.empty())
      return;

   if (numSourceChannels == 1) {
      // Case 2: mono source replicates into every set bit in range.
      const float volume = getChannelVolume(0);
      ForEachSetBitInRange(mask, numOutputChannels,
         [&](unsigned outCh) {
            for (size_t i = 0; i < samplesAvailable; ++i)
               masterBuffers[outCh][i] +=
                  processingBuffers[0][i] * volume;
         });
      return;
   }

   // Case 3: multi-channel source walks set bits sequentially.  Each
   // source channel pairs with the next set bit; extras are dropped.
   unsigned srcChannel = 0;
   ForEachSetBitInRange(mask, numOutputChannels,
      [&](unsigned outCh) {
         if (srcChannel >= numSourceChannels)
            return; // out of source channels; remaining bits get nothing
         const float volume =
            getChannelVolume(static_cast<int>(srcChannel));
         for (size_t i = 0; i < samplesAvailable; ++i) {
            processingBuffers[srcChannel][i] *= volume;
            masterBuffers[outCh][i] += processingBuffers[srcChannel][i];
         }
         ++srcChannel;
      });
}
