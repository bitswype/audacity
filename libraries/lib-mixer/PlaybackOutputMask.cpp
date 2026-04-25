/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackOutputMask.cpp

*******************************************************************/
#include "PlaybackOutputMask.h"

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
      // shift is in [0, 64) here -- the 128-cap branch above handled
      // numDeviceChannels >= 128 already, so shift == 64 cannot occur.
      const unsigned shift = numDeviceChannels - 64;
      const uint64_t inRange = (uint64_t(1) << shift) - 1;
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

PlaybackOutputMask AtomicPlaybackOutputMask::Load() const noexcept
{
   // Seqlock read pattern: keep retrying until we observe a stable
   // even sequence number bracketing both halves of the value.
   for (;;) {
      const uint64_t s1 = mSeq.load(std::memory_order_acquire);
      // Odd seq means a writer is mid-update; spin briefly and retry.
      // This is bounded under single-writer because the writer always
      // advances seq forward.
      if (s1 & uint64_t(1))
         continue;
      const uint64_t lo = mLo.load(std::memory_order_relaxed);
      const uint64_t hi = mHi.load(std::memory_order_relaxed);
      // Acquire fence pairs with the writer's release on the trailing
      // seq store; ensures lo/hi values we just read can't be
      // reordered after the seq re-read below.
      std::atomic_thread_fence(std::memory_order_acquire);
      const uint64_t s2 = mSeq.load(std::memory_order_relaxed);
      if (s1 == s2)
         return PlaybackOutputMask{ lo, hi };
      // Sequence advanced between our two reads -- a writer published
      // a new value somewhere in the middle.  Retry.
   }
}

void AtomicPlaybackOutputMask::Store(PlaybackOutputMask mask) noexcept
{
   // Single-writer: relaxed read of seq is OK, no other writer can
   // observe-and-modify in between.  We bracket the lo/hi stores with
   // odd (mid-write) and then-even (committed) seq values.
   const uint64_t s = mSeq.load(std::memory_order_relaxed);
   mSeq.store(s + 1, std::memory_order_relaxed);
   // Release fence ensures the lo/hi stores below cannot be reordered
   // before the odd-seq store above (so a reader that sees the odd seq
   // also sees no writer-side stores of the new value yet).
   std::atomic_thread_fence(std::memory_order_release);
   mLo.store(mask.lo, std::memory_order_relaxed);
   mHi.store(mask.hi, std::memory_order_relaxed);
   // Release on the trailing seq store publishes lo/hi to readers.
   mSeq.store(s + 2, std::memory_order_release);
}

namespace
{
//! Highest set bit + 1 across all masks; 0 if none have any bits set.
unsigned MaxSetBitPlusOne(
   const std::vector<PlaybackOutputMask>& masks)
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

unsigned ComputeRoutingDialogColumnCount(
   unsigned deviceChannels,
   const std::vector<PlaybackOutputMask>& trackMasks,
   const std::vector<unsigned>& trackChannelCounts)
{
   // Catch caller bugs in debug; in release fall back to the shorter
   // vector to avoid OOB access (silent truncation), but the API
   // contract is "same size".
   assert(trackMasks.size() == trackChannelCounts.size());

   unsigned columns = std::max(2u, deviceChannels);
   columns = std::max(columns, MaxSetBitPlusOne(trackMasks));

   // Every row r with channel count c must be Reset-able: identity
   // routing for it sets bits r..r+c-1, so the dialog needs at least
   // r+c columns to expose those bits.
   const auto rowCount = std::min(
      trackMasks.size(), trackChannelCounts.size());
   for (size_t r = 0; r < rowCount; ++r) {
      const auto c = trackChannelCounts[r];
      if (c == 0)
         continue;
      const auto need = static_cast<unsigned>(r) + c;
      columns = std::max(columns, need);
   }
   return std::min(columns, kPlaybackOutputMaskBits);
}

unsigned CountTracksWithBitsAboveDeviceWidth(
   unsigned deviceChannels,
   const std::vector<PlaybackOutputMask>& trackMasks)
{
   unsigned count = 0;
   for (const auto& m : trackMasks) {
      if (m.hasBitsAboveDeviceWidth(deviceChannels))
         ++count;
   }
   return count;
}
