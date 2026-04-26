/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  RouteRecordingSamplesTest.cpp

  Tests for the dual-of-RouteTrackSamples recording helper.  Verifies
  the mono-SUM, multi-1:1, popcount<channels, popcount>channels,
  empty-mask, and bits-beyond-device cases.

**********************************************************************/
#include "RouteRecordingSamples.h"

#include <catch2/catch.hpp>

#include <array>
#include <vector>

namespace
{
constexpr size_t kSamples = 8;

//! Build a stereotype set of staging buffers where buffer N is filled
//! with sample-value (N + 1) * 0.1f for easy summing arithmetic.
std::vector<std::vector<float>> MakeStagingValues(size_t numChannels)
{
   std::vector<std::vector<float>> v(numChannels);
   for (size_t ch = 0; ch < numChannels; ++ch) {
      v[ch].resize(kSamples);
      const float val = static_cast<float>(ch + 1) * 0.1f;
      for (size_t i = 0; i < kSamples; ++i)
         v[ch][i] = val;
   }
   return v;
}

std::vector<const float*> Pointers(
   const std::vector<std::vector<float>>& staging)
{
   std::vector<const float*> p;
   p.reserve(staging.size());
   for (const auto& v : staging)
      p.push_back(v.data());
   return p;
}
} // namespace

TEST_CASE("RouteRecordingSamples: empty mask -> silence",
   "[RouteRecordingSamples]")
{
   const auto staging = MakeStagingValues(8);
   const auto p = Pointers(staging);

   std::array<float, kSamples> dest{};
   for (auto& f : dest) f = 999.0f; // poison

   PlaybackInputMask m; // empty
   RouteRecordingSamples(m, /*numTrackChannels=*/1, /*trackChannel=*/0,
      /*numDeviceChannels=*/8, kSamples, p.data(), dest.data());

   for (auto v : dest) CHECK(v == 0.0f);
}

TEST_CASE("RouteRecordingSamples: mono SUM of multiple inputs",
   "[RouteRecordingSamples]")
{
   const auto staging = MakeStagingValues(8); // ch N -> (N+1)*0.1
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(1); // 0.2
   m.set(3); // 0.4
   m.set(5); // 0.6

   std::array<float, kSamples> dest{};
   RouteRecordingSamples(m, /*numTrackChannels=*/1, /*trackChannel=*/0,
      /*numDeviceChannels=*/8, kSamples, p.data(), dest.data());

   const float expected = 0.2f + 0.4f + 0.6f;
   for (auto v : dest)
      CHECK(v == Approx(expected));
}

TEST_CASE("RouteRecordingSamples: mono SUM ignores bits beyond device",
   "[RouteRecordingSamples]")
{
   const auto staging = MakeStagingValues(4); // only 4 device channels
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(1); // 0.2 (in range)
   m.set(5); // out of range (only 4 channels)
   m.set(70); // out of range

   std::array<float, kSamples> dest{};
   RouteRecordingSamples(m, /*numTrackChannels=*/1, /*trackChannel=*/0,
      /*numDeviceChannels=*/4, kSamples, p.data(), dest.data());

   for (auto v : dest)
      CHECK(v == Approx(0.2f));
}

TEST_CASE("RouteRecordingSamples: multi-channel 1:1 walks bits in order",
   "[RouteRecordingSamples]")
{
   const auto staging = MakeStagingValues(8);
   const auto p = Pointers(staging);

   // Mask sets bits 2, 5, 6: ch 0 <- bit 2 (0.3), ch 1 <- bit 5 (0.6),
   // ch 2 <- bit 6 (0.7).  Track has 3 channels.
   PlaybackInputMask m;
   m.set(2);
   m.set(5);
   m.set(6);

   std::array<float, kSamples> ch0{}, ch1{}, ch2{};
   RouteRecordingSamples(m, 3, 0, 8, kSamples, p.data(), ch0.data());
   RouteRecordingSamples(m, 3, 1, 8, kSamples, p.data(), ch1.data());
   RouteRecordingSamples(m, 3, 2, 8, kSamples, p.data(), ch2.data());

   for (auto v : ch0) CHECK(v == Approx(0.3f));
   for (auto v : ch1) CHECK(v == Approx(0.6f));
   for (auto v : ch2) CHECK(v == Approx(0.7f));
}

TEST_CASE(
   "RouteRecordingSamples: multi with fewer set bits than channels",
   "[RouteRecordingSamples]")
{
   // Stereo track but only 1 set bit -> ch 0 gets bit 4, ch 1 silent.
   const auto staging = MakeStagingValues(8);
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(4);

   std::array<float, kSamples> ch0{}, ch1{};
   RouteRecordingSamples(m, 2, 0, 8, kSamples, p.data(), ch0.data());
   RouteRecordingSamples(m, 2, 1, 8, kSamples, p.data(), ch1.data());

   for (auto v : ch0) CHECK(v == Approx(0.5f));
   for (auto v : ch1) CHECK(v == 0.0f);
}

TEST_CASE(
   "RouteRecordingSamples: multi with more set bits than channels drops extras",
   "[RouteRecordingSamples]")
{
   // Stereo track, 4 set bits -> ch 0 and 1 take first 2; bits 2/3 dropped.
   // (Caller is responsible for not invoking us with trackChannel >=
   // numTrackChannels; we test that the remaining bits don't leak in.)
   const auto staging = MakeStagingValues(8);
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(0); // 0.1
   m.set(1); // 0.2
   m.set(7); // 0.8
   m.set(6); // 0.7

   std::array<float, kSamples> ch0{}, ch1{};
   RouteRecordingSamples(m, 2, 0, 8, kSamples, p.data(), ch0.data());
   RouteRecordingSamples(m, 2, 1, 8, kSamples, p.data(), ch1.data());

   // ch 0 gets the lowest set bit (0); ch 1 the next lowest (1).
   for (auto v : ch0) CHECK(v == Approx(0.1f));
   for (auto v : ch1) CHECK(v == Approx(0.2f));
}

TEST_CASE("RouteRecordingSamples: numTrackChannels == 0 leaves silence",
   "[RouteRecordingSamples]")
{
   const auto staging = MakeStagingValues(4);
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(2);

   std::array<float, kSamples> dest{};
   RouteRecordingSamples(m, 0, 0, 4, kSamples, p.data(), dest.data());
   for (auto v : dest) CHECK(v == 0.0f);
}

TEST_CASE(
   "RouteRecordingSamples: multi-track overdub-shaped flat-walk",
   "[RouteRecordingSamples]")
{
   // Regression test for the matrix-mode wiring in
   // AudioIO::DrainRecordBuffersMatrix: the per-pass loop walks
   // (track, channel) destinations as a flat list and calls
   // RouteRecordingSamples for each.  This test reproduces that
   // walk against the same helper, with a mix of mono SUM, stereo
   // 1:1, and a track whose mask has fewer bits than channels, to
   // pin the destination ordering and per-track-channel sample
   // value.  Documents the overdub+matrix flow that the engine
   // assumes works.
   //
   // Configuration:
   //   Track 0: mono,    mask {1, 3}    -> dest 0: stage[1]+stage[3]
   //   Track 1: stereo,  mask {2, 5}    -> dest 1: stage[2]
   //                                      dest 2: stage[5]
   //   Track 2: stereo,  mask {7}       -> dest 3: stage[7]
   //                                      dest 4: silence (popcount<channels)
   //   Track 3: mono,    mask {}  empty -> SKIPPED (not a target)

   const auto staging = MakeStagingValues(8); // ch N -> (N+1)*0.1f
   const auto p = Pointers(staging);

   struct TrackPlan {
      PlaybackInputMask mask;
      size_t numChannels;
   };
   std::vector<TrackPlan> plans;
   {
      PlaybackInputMask m;
      m.set(1); m.set(3);
      plans.push_back({m, 1});
   }
   {
      PlaybackInputMask m;
      m.set(2); m.set(5);
      plans.push_back({m, 2});
   }
   {
      PlaybackInputMask m;
      m.set(7);
      plans.push_back({m, 2});
   }
   plans.push_back({PlaybackInputMask{}, 1}); // empty mask, will skip

   // Walk the flat (track, channel) list exactly the way
   // DrainRecordBuffersMatrix does.  Skip empty-mask tracks.
   std::vector<std::array<float, kSamples>> destBuffers;
   for (const auto& plan : plans) {
      if (plan.mask.empty())
         continue;
      for (size_t ch = 0; ch < plan.numChannels; ++ch) {
         std::array<float, kSamples> dest{};
         RouteRecordingSamples(plan.mask, plan.numChannels, ch,
            /*numDeviceChannels=*/8, kSamples, p.data(), dest.data());
         destBuffers.push_back(dest);
      }
   }

   // Five live destinations expected: 1 + 2 + 2 (the empty-mask
   // track contributes none).
   REQUIRE(destBuffers.size() == 5u);

   // dest 0: mono SUM of bits 1,3 -> 0.2 + 0.4 = 0.6
   for (auto v : destBuffers[0]) CHECK(v == Approx(0.6f));
   // dest 1: stereo ch 0 -> first set bit 2 -> 0.3
   for (auto v : destBuffers[1]) CHECK(v == Approx(0.3f));
   // dest 2: stereo ch 1 -> second set bit 5 -> 0.6
   for (auto v : destBuffers[2]) CHECK(v == Approx(0.6f));
   // dest 3: stereo ch 0 -> only set bit 7 -> 0.8
   for (auto v : destBuffers[3]) CHECK(v == Approx(0.8f));
   // dest 4: stereo ch 1 -> no second set bit -> silence
   for (auto v : destBuffers[4]) CHECK(v == 0.0f);
}

TEST_CASE("RouteRecordingSamples: cross-word boundary mono SUM",
   "[RouteRecordingSamples]")
{
   // Need at least 70 staging buffers to test bit 65 -- give them
   // distinct values.
   constexpr size_t numCh = 80;
   std::vector<std::vector<float>> staging(numCh);
   for (size_t i = 0; i < numCh; ++i) {
      staging[i].resize(kSamples);
      for (size_t s = 0; s < kSamples; ++s)
         staging[i][s] = static_cast<float>(i);
   }
   const auto p = Pointers(staging);

   PlaybackInputMask m;
   m.set(63);
   m.set(64);
   m.set(70);

   std::array<float, kSamples> dest{};
   RouteRecordingSamples(m, /*numTrackChannels=*/1, /*trackChannel=*/0,
      /*numDeviceChannels=*/numCh, kSamples, p.data(), dest.data());

   const float expected = 63.0f + 64.0f + 70.0f;
   for (auto v : dest) CHECK(v == Approx(expected));
}
