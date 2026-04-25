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
