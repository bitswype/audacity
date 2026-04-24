/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackOutputMask.cpp

*******************************************************************/
#include "PlaybackOutputMask.h"

#if defined(__GNUC__) || defined(__clang__)
   #define AU_POPCOUNT64(x) __builtin_popcountll(x)
   #define AU_CLZ64(x)      __builtin_clzll(x)
#else
   #include <intrin.h>
   static inline int AU_POPCOUNT64(uint64_t x)
   {
      return static_cast<int>(__popcnt64(x));
   }
   static inline int AU_CLZ64(uint64_t x)
   {
      unsigned long idx;
      _BitScanReverse64(&idx, x);
      return 63 - static_cast<int>(idx);
   }
#endif

unsigned PlaybackOutputMask::popcount() const
{
   return static_cast<unsigned>(AU_POPCOUNT64(lo) + AU_POPCOUNT64(hi));
}

bool PlaybackOutputMask::hasBitsAboveDeviceWidth(
   unsigned numDeviceChannels) const
{
   if (numDeviceChannels >= kPlaybackOutputMaskBits)
      return false;
   if (numDeviceChannels >= 64) {
      // All of lo is in range; check the high word past the boundary.
      const unsigned shift = numDeviceChannels - 64;
      // Mask off the bits that are within range.
      const uint64_t inRange = (shift == 64)
         ? ~uint64_t(0)
         : ((uint64_t(1) << shift) - 1);
      return (hi & ~inRange) != 0;
   }
   // Some bits of lo are out of range, all of hi is out of range.
   const uint64_t inRange = (numDeviceChannels == 0)
      ? 0
      : ((uint64_t(1) << numDeviceChannels) - 1);
   return (lo & ~inRange) != 0 || hi != 0;
}

PlaybackOutputMask PlaybackOutputMask::Identity(
   unsigned firstChannel, unsigned channelCount)
{
   PlaybackOutputMask m;
   for (unsigned n = 0; n < channelCount; ++n) {
      const unsigned bit = firstChannel + n;
      if (bit >= kPlaybackOutputMaskBits)
         break;
      m.set(bit);
   }
   return m;
}
