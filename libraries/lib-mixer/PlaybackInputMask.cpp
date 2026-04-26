/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackInputMask.cpp

*******************************************************************/
#include "PlaybackInputMask.h"

#include <algorithm>
#include <cassert>

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

unsigned PlaybackInputMask::popcount() const
{
   return static_cast<unsigned>(AU_POPCOUNT64(lo) + AU_POPCOUNT64(hi));
}

bool PlaybackInputMask::hasBitsAboveDeviceWidth(
   unsigned numDeviceChannels) const
{
   if (numDeviceChannels >= kPlaybackInputMaskBits)
      return false;
   if (numDeviceChannels >= 64) {
      // shift in [0, 64) here; the cap branch above handled >= 128.
      const unsigned shift = numDeviceChannels - 64;
      const uint64_t inRange = (uint64_t(1) << shift) - 1;
      return (hi & ~inRange) != 0;
   }
   const uint64_t inRange = (numDeviceChannels == 0)
      ? 0
      : ((uint64_t(1) << numDeviceChannels) - 1);
   return (lo & ~inRange) != 0 || hi != 0;
}

PlaybackInputMask PlaybackInputMask::Identity(
   unsigned firstChannel, unsigned channelCount)
{
   PlaybackInputMask m;
   for (unsigned n = 0; n < channelCount; ++n) {
      const unsigned bit = firstChannel + n;
      if (bit >= kPlaybackInputMaskBits)
         break;
      m.set(bit);
   }
   return m;
}

PlaybackInputMask AtomicPlaybackInputMask::Load() const noexcept
{
   for (;;) {
      const uint64_t s1 = mSeq.load(std::memory_order_acquire);
      if (s1 & uint64_t(1))
         continue;
      const uint64_t lo = mLo.load(std::memory_order_relaxed);
      const uint64_t hi = mHi.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      const uint64_t s2 = mSeq.load(std::memory_order_relaxed);
      if (s1 == s2)
         return PlaybackInputMask{ lo, hi };
   }
}

void AtomicPlaybackInputMask::Store(PlaybackInputMask mask) noexcept
{
   const uint64_t s = mSeq.load(std::memory_order_relaxed);
   mSeq.store(s + 1, std::memory_order_relaxed);
   std::atomic_thread_fence(std::memory_order_release);
   mLo.store(mask.lo, std::memory_order_relaxed);
   mHi.store(mask.hi, std::memory_order_relaxed);
   mSeq.store(s + 2, std::memory_order_release);
}

namespace
{
unsigned MaxSetBitPlusOne(
   const std::vector<PlaybackInputMask>& masks)
{
   unsigned highest = 0;
   for (const auto& m : masks) {
      if (m.hi != 0)
         highest = std::max(highest,
            64u + 64u - static_cast<unsigned>(AU_CLZ64(m.hi)));
      else if (m.lo != 0)
         highest = std::max(highest,
            64u - static_cast<unsigned>(AU_CLZ64(m.lo)));
   }
   return highest;
}
} // namespace

unsigned ComputeRecordingDialogColumnCount(
   unsigned deviceChannels,
   const std::vector<PlaybackInputMask>& trackMasks,
   const std::vector<unsigned>& trackChannelCounts)
{
   assert(trackMasks.size() == trackChannelCounts.size());

   unsigned columns = std::max(2u, deviceChannels);
   columns = std::max(columns, MaxSetBitPlusOne(trackMasks));

   const auto rowCount = std::min(
      trackMasks.size(), trackChannelCounts.size());
   for (size_t r = 0; r < rowCount; ++r) {
      const auto c = trackChannelCounts[r];
      if (c == 0)
         continue;
      const auto need = static_cast<unsigned>(r) + c;
      columns = std::max(columns, need);
   }
   return std::min(columns, kPlaybackInputMaskBits);
}

unsigned CountRecordingTracksWithBitsAboveDeviceWidth(
   unsigned deviceChannels,
   const std::vector<PlaybackInputMask>& trackMasks)
{
   unsigned count = 0;
   for (const auto& m : trackMasks) {
      if (m.hasBitsAboveDeviceWidth(deviceChannels))
         ++count;
   }
   return count;
}
