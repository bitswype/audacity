/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  AtomicPlaybackOutputMaskTest.cpp

  Tests for the seqlock-backed AtomicPlaybackOutputMask wrapper.
  Includes single-threaded round-trips and a multi-threaded torn-read
  detector that hammers Store on one thread while another thread
  reads in a tight loop, asserting the (lo, hi) pair always
  corresponds to a value that was actually stored.

**********************************************************************/
#include "PlaybackOutputMask.h"

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("AtomicPlaybackOutputMask: default Load is empty",
   "[AtomicPlaybackOutputMask]")
{
   AtomicPlaybackOutputMask a;
   CHECK(a.Load().empty());
}

TEST_CASE("AtomicPlaybackOutputMask: round-trip simple values",
   "[AtomicPlaybackOutputMask]")
{
   AtomicPlaybackOutputMask a;

   PlaybackOutputMask m;
   m.set(0);
   a.Store(m);
   CHECK(a.Load() == m);

   m.set(63);
   m.set(64);
   m.set(127);
   a.Store(m);
   CHECK(a.Load() == m);

   a.Store({});
   CHECK(a.Load().empty());
}

TEST_CASE("AtomicPlaybackOutputMask: round-trip cross-word values",
   "[AtomicPlaybackOutputMask]")
{
   // Several distinct (lo, hi) pairs to exercise the bit boundary
   // and the all-ones / all-zeros corners.
   const std::vector<PlaybackOutputMask> values{
      PlaybackOutputMask{ 0, 0 },
      PlaybackOutputMask{ 0xFFFFFFFFFFFFFFFFULL, 0 },
      PlaybackOutputMask{ 0, 0xFFFFFFFFFFFFFFFFULL },
      PlaybackOutputMask{ 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL },
      PlaybackOutputMask{ 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL },
      PlaybackOutputMask{ 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL },
   };
   AtomicPlaybackOutputMask a;
   for (const auto &v : values) {
      a.Store(v);
      CHECK(a.Load() == v);
   }
}

TEST_CASE("AtomicPlaybackOutputMask: copy constructor snapshots value",
   "[AtomicPlaybackOutputMask]")
{
   AtomicPlaybackOutputMask src;
   PlaybackOutputMask m{ 0xDEADBEEFULL, 0xCAFEBABEULL };
   src.Store(m);

   AtomicPlaybackOutputMask copy{ src };
   CHECK(copy.Load() == m);

   // Mutating src after copy must not affect the copy.
   src.Store(PlaybackOutputMask{ 0, 0 });
   CHECK(copy.Load() == m);
   CHECK(src.Load().empty());
}

TEST_CASE("AtomicPlaybackOutputMask: copy assignment snapshots value",
   "[AtomicPlaybackOutputMask]")
{
   AtomicPlaybackOutputMask src;
   AtomicPlaybackOutputMask dst;
   PlaybackOutputMask m{ 0x1111222233334444ULL, 0x5555666677778888ULL };
   src.Store(m);
   dst = src;
   CHECK(dst.Load() == m);

   // Self-assignment must be safe and a no-op.
   dst = dst;
   CHECK(dst.Load() == m);
}

TEST_CASE(
   "AtomicPlaybackOutputMask: concurrent reader observes no torn values",
   "[AtomicPlaybackOutputMask][.stress]")
{
   // The writer cycles through a set of values where lo and hi are
   // strongly correlated (hi = ~lo).  A torn read would observe a
   // (lo, hi) pair where this invariant is broken.  We additionally
   // verify that every observed value is one of the values we
   // actually stored.
   AtomicPlaybackOutputMask atom;
   std::atomic<bool> stop{ false };
   std::atomic<size_t> tornReads{ 0 };
   std::atomic<size_t> unknownValues{ 0 };
   std::atomic<size_t> readCount{ 0 };
   std::atomic<size_t> writeCount{ 0 };

   constexpr int kPatterns = 8;
   const auto patternFor = [](int i) {
      // Distinct values with hi = ~lo invariant.
      const uint64_t lo = uint64_t(1) << i;
      return PlaybackOutputMask{ lo, ~lo };
   };

   // Pre-publish the first pattern so reader's initial Load() is one
   // of the known patterns.
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
            // hi = ~lo invariant must hold for every stored value.
            if (v.hi != ~v.lo) {
               tornReads.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            // Match against the discrete set of patterns.
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
   for (auto &t : readers)
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

TEST_CASE(
   "AtomicPlaybackOutputMask: rapid empty<->full alternation",
   "[AtomicPlaybackOutputMask][.stress]")
{
   // A reader that sees a torn empty<->full transition would observe
   // (lo, hi) of (0, ~0) or (~0, 0).  Track those explicitly.
   AtomicPlaybackOutputMask atom;
   std::atomic<bool> stop{ false };
   std::atomic<size_t> tornCount{ 0 };
   const PlaybackOutputMask full{
      0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL };
   const PlaybackOutputMask empty{ 0, 0 };
   atom.Store(empty);

   std::thread writer([&] {
      bool toggle = false;
      while (!stop.load(std::memory_order_relaxed)) {
         atom.Store(toggle ? full : empty);
         toggle = !toggle;
      }
   });

   std::thread reader([&] {
      while (!stop.load(std::memory_order_relaxed)) {
         const auto v = atom.Load();
         if (v != empty && v != full)
            tornCount.fetch_add(1, std::memory_order_relaxed);
      }
   });

   std::this_thread::sleep_for(std::chrono::milliseconds(500));
   stop.store(true, std::memory_order_relaxed);
   writer.join();
   reader.join();

   CHECK(tornCount.load() == 0);
}
