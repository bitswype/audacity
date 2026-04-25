/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  AtomicPlaybackInputMaskTest.cpp

  Mirrors AtomicPlaybackOutputMaskTest.cpp but for the recording-side
  type.  Verifies seqlock round-trips, copy semantics, and concurrent
  reader / writer torn-read detection.

**********************************************************************/
#include "PlaybackInputMask.h"

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("AtomicPlaybackInputMask: default Load is empty",
   "[AtomicPlaybackInputMask]")
{
   AtomicPlaybackInputMask a;
   CHECK(a.Load().empty());
}

TEST_CASE("AtomicPlaybackInputMask: round-trip values",
   "[AtomicPlaybackInputMask]")
{
   AtomicPlaybackInputMask a;
   const std::vector<PlaybackInputMask> values{
      PlaybackInputMask{ 0, 0 },
      PlaybackInputMask{ 0xFFFFFFFFFFFFFFFFULL, 0 },
      PlaybackInputMask{ 0, 0xFFFFFFFFFFFFFFFFULL },
      PlaybackInputMask{ 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL },
      PlaybackInputMask{ 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL },
      PlaybackInputMask{ 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL },
   };
   for (const auto& v : values) {
      a.Store(v);
      CHECK(a.Load() == v);
   }
}

TEST_CASE("AtomicPlaybackInputMask: copy ctor snapshots value",
   "[AtomicPlaybackInputMask]")
{
   AtomicPlaybackInputMask src;
   PlaybackInputMask m{ 0xDEADBEEFULL, 0xCAFEBABEULL };
   src.Store(m);

   AtomicPlaybackInputMask copy{ src };
   CHECK(copy.Load() == m);

   src.Store(PlaybackInputMask{ 0, 0 });
   CHECK(copy.Load() == m);
   CHECK(src.Load().empty());
}

TEST_CASE("AtomicPlaybackInputMask: copy assignment snapshots value",
   "[AtomicPlaybackInputMask]")
{
   AtomicPlaybackInputMask src, dst;
   PlaybackInputMask m{ 0x1111222233334444ULL, 0x5555666677778888ULL };
   src.Store(m);
   dst = src;
   CHECK(dst.Load() == m);
   dst = dst;
   CHECK(dst.Load() == m);
}

TEST_CASE(
   "AtomicPlaybackInputMask: concurrent reader observes no torn values",
   "[AtomicPlaybackInputMask][.stress]")
{
   AtomicPlaybackInputMask atom;
   std::atomic<bool> stop{ false };
   std::atomic<size_t> tornReads{ 0 };
   std::atomic<size_t> unknownValues{ 0 };
   std::atomic<size_t> readCount{ 0 };
   std::atomic<size_t> writeCount{ 0 };

   constexpr int kPatterns = 8;
   const auto patternFor = [](int i) {
      const uint64_t lo = uint64_t(1) << i;
      return PlaybackInputMask{ lo, ~lo };
   };

   atom.Store(patternFor(0));

   std::thread writer([&] {
      int i = 0;
      while (!stop.load(std::memory_order_relaxed)) {
         atom.Store(patternFor(i % kPatterns));
         ++i;
         writeCount.fetch_add(1, std::memory_order_relaxed);
      }
   });

   std::vector<std::thread> readers;
   constexpr int kReaderCount = 4;
   for (int r = 0; r < kReaderCount; ++r) {
      readers.emplace_back([&] {
         while (!stop.load(std::memory_order_relaxed)) {
            const auto v = atom.Load();
            readCount.fetch_add(1, std::memory_order_relaxed);
            if (v.hi != ~v.lo) {
               tornReads.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            bool found = false;
            for (int i = 0; i < kPatterns; ++i) {
               if (v == patternFor(i)) {
                  found = true;
                  break;
               }
            }
            if (!found)
               unknownValues.fetch_add(1, std::memory_order_relaxed);
         }
      });
   }

   std::this_thread::sleep_for(std::chrono::milliseconds(500));
   stop.store(true, std::memory_order_relaxed);
   writer.join();
   for (auto& t : readers)
      t.join();

   INFO("reads=" << readCount.load()
        << " writes=" << writeCount.load()
        << " torn=" << tornReads.load()
        << " unknown=" << unknownValues.load());
   CHECK(readCount.load() > 0);
   CHECK(writeCount.load() > 0);
   CHECK(tornReads.load() == 0);
   CHECK(unknownValues.load() == 0);
}
