/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamplesTest.cpp

  Tests for the sample-distribution loop used by AudioIO.  Three cases
  after the 128-bit static-mask refactor:
    1. Empty mask -> silent (masters untouched).
    2. Mono source -> replicated to every set bit in range.
    3. Multi-channel source -> source channels walk set bits in order.

**********************************************************************/
#include "RouteTrackSamples.h"

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace {

class RoutingFixture
{
public:
   RoutingFixture(size_t numSourceChannels, size_t numOutputChannels,
                  size_t samplesAvailable)
      : procStorage(numSourceChannels,
                    std::vector<float>(samplesAvailable, 0.f))
      , masterStorage(numOutputChannels,
                      std::vector<float>(samplesAvailable, 0.f))
      , procPtrs(numSourceChannels, nullptr)
      , masterPtrs(numOutputChannels, nullptr)
      , samplesAvailable(samplesAvailable)
   {
      for (size_t i = 0; i < procStorage.size(); ++i)
         procPtrs[i] = procStorage[i].data();
      for (size_t i = 0; i < masterStorage.size(); ++i)
         masterPtrs[i] = masterStorage[i].data();
   }

   void fillSource(size_t ch, float value)
   {
      std::fill(procStorage[ch].begin(), procStorage[ch].end(), value);
   }

   float master(size_t ch, size_t i = 0) const
   { return masterStorage[ch][i]; }
   float source(size_t ch, size_t i = 0) const
   { return procStorage[ch][i]; }

   float* const* procBuffers() { return procPtrs.data(); }
   float* const* masterBuffers() { return masterPtrs.data(); }

   const size_t samplesAvailable;

private:
   std::vector<std::vector<float>> procStorage;
   std::vector<std::vector<float>> masterStorage;
   std::vector<float*> procPtrs;
   std::vector<float*> masterPtrs;
};

auto unitVolume = [](int) { return 1.f; };

TrackChannelAssignment WithMask(uint64_t lo, uint64_t hi = 0)
{
   TrackChannelAssignment a;
   a.outputMask = PlaybackOutputMask{ lo, hi };
   return a;
}

} // namespace

TEST_CASE("Empty mask produces silence (mono source)",
   "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 5.f);
   TrackChannelAssignment assignment{}; // empty mask

   RouteTrackSamples(assignment, 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   for (size_t ch = 0; ch < 4; ++ch)
      REQUIRE(f.master(ch) == 0.f);
   // Source not mutated in the silent case.
   REQUIRE(f.source(0) == 5.f);
}

TEST_CASE("Empty mask produces silence (stereo source)",
   "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 2.f);
   f.fillSource(1, 3.f);
   TrackChannelAssignment assignment{};

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   for (size_t ch = 0; ch < 4; ++ch)
      REQUIRE(f.master(ch) == 0.f);
   REQUIRE(f.source(0) == 2.f);
   REQUIRE(f.source(1) == 3.f);
}

TEST_CASE("Mono mask routes to a single output", "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 2.f);

   RouteTrackSamples(WithMask(0b0001), 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 2.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Mono mask duplicates to multiple outputs",
   "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 3.f);

   RouteTrackSamples(WithMask(0b0101), 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 3.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 3.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Mono mask ignores bits past the device width",
   "[RouteTrackSamples]")
{
   // Mask has bit 5 set but the device only has 4 channels.  The set
   // bit is silently dropped.
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 5.f);

   RouteTrackSamples(WithMask(0b00100001), 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 5.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Stereo source walks two set bits", "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 4.f);

   RouteTrackSamples(WithMask(0b0011), 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 1.f);
   REQUIRE(f.master(1) == 4.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Stereo source with one mask bit drops the extra source",
   "[RouteTrackSamples]")
{
   // Only one bit set -> only source channel 0 routes; channel 1 is
   // dropped (no more set bits).
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 2.f);

   RouteTrackSamples(WithMask(0b0100), 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 1.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE(
   "4-channel with split mask distributes sequentially across bits",
   "[RouteTrackSamples]")
{
   // mask = 0b1100'0011 on 8-channel device:
   //   source 0 -> bit 0 (output 0)
   //   source 1 -> bit 1 (output 1)
   //   source 2 -> bit 6 (output 6)
   //   source 3 -> bit 7 (output 7)
   RoutingFixture f(4, 8, 8);
   f.fillSource(0, 10.f);
   f.fillSource(1, 20.f);
   f.fillSource(2, 30.f);
   f.fillSource(3, 40.f);

   RouteTrackSamples(WithMask(0b11000011), 4, 8, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 10.f);
   REQUIRE(f.master(1) == 20.f);
   REQUIRE(f.master(6) == 30.f);
   REQUIRE(f.master(7) == 40.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
   REQUIRE(f.master(4) == 0.f);
   REQUIRE(f.master(5) == 0.f);
}

TEST_CASE("Stereo with three set bits drops the extra bit",
   "[RouteTrackSamples]")
{
   // 3 set bits but only 2 source channels.  The first two bits get
   // source 0 and 1; the third bit gets nothing (sources exhausted).
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 5.f);

   RouteTrackSamples(WithMask(0b0111), 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 1.f);
   REQUIRE(f.master(1) == 5.f);
   REQUIRE(f.master(2) == 0.f); // no source left
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Mono mask applies per-channel volume (source index 0)",
   "[RouteTrackSamples]")
{
   RoutingFixture f(1, 2, 8);
   f.fillSource(0, 1.f);
   auto getVolume = [](int ch) { return ch == 0 ? 0.5f : -999.f; };

   RouteTrackSamples(WithMask(0b11), 1, 2, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == Catch::Detail::Approx(0.5f));
   REQUIRE(f.master(1) == Catch::Detail::Approx(0.5f));
}

TEST_CASE("Multi-channel mask applies per-source-channel volume",
   "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 1.f);
   auto getVolume = [](int ch) { return ch == 0 ? 0.5f : 2.0f; };

   RouteTrackSamples(WithMask(0b0011), 2, 4, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == Catch::Detail::Approx(0.5f));
   REQUIRE(f.master(1) == Catch::Detail::Approx(2.0f));
}

TEST_CASE("Mono mask does not mutate processingBuffers",
   "[RouteTrackSamples]")
{
   RoutingFixture f(1, 2, 4);
   f.fillSource(0, 7.f);
   auto getVolume = [](int) { return 3.f; };

   RouteTrackSamples(WithMask(0b11), 1, 2, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 21.f);
   REQUIRE(f.source(0) == 7.f); // unchanged
}

TEST_CASE("Multi-channel mask mutates processingBuffers in place",
   "[RouteTrackSamples]")
{
   // Historical quirk preserved from the pre-refactor engine: the
   // multi-channel path multiplies source buffers by the per-channel
   // volume in place before accumulating.
   RoutingFixture f(2, 2, 4);
   f.fillSource(0, 1.f);
   f.fillSource(1, 1.f);
   auto getVolume = [](int ch) { return ch == 0 ? 0.25f : 0.5f; };

   RouteTrackSamples(WithMask(0b11), 2, 2, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.source(0) == Catch::Detail::Approx(0.25f));
   REQUIRE(f.source(1) == Catch::Detail::Approx(0.50f));
}

TEST_CASE("Multiple tracks summed into the same output",
   "[RouteTrackSamples]")
{
   RoutingFixture f1(1, 2, 4);
   f1.fillSource(0, 1.f);
   RoutingFixture f2(1, 2, 4);
   f2.fillSource(0, 3.f);

   std::vector<float> m0(4, 0.f);
   std::vector<float> m1(4, 0.f);
   std::array<float*, 2> masters = { m0.data(), m1.data() };

   RouteTrackSamples(WithMask(0b01), 1, 2, 4, unitVolume,
                     f1.procBuffers(), masters.data());
   RouteTrackSamples(WithMask(0b01), 1, 2, 4, unitVolume,
                     f2.procBuffers(), masters.data());

   REQUIRE(m0[0] == 4.f);
   REQUIRE(m1[0] == 0.f);
}

TEST_CASE("High-word bit: mono source routed to bit 64",
   "[RouteTrackSamples]")
{
   // Device has 128 outputs (max mask width).  Set only bit 64.
   RoutingFixture f(1, 128, 4);
   f.fillSource(0, 9.f);

   RouteTrackSamples(WithMask(0, uint64_t(1)), 1, 128,
                     f.samplesAvailable, unitVolume,
                     f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(63) == 0.f);
   REQUIRE(f.master(64) == 9.f);
   REQUIRE(f.master(65) == 0.f);
}

TEST_CASE("Stereo source spanning the lo/hi boundary",
   "[RouteTrackSamples]")
{
   // Bits 63 and 64 set: source 0 -> bit 63 (lo word top), source 1
   // -> bit 64 (hi word bottom).  Tests the loop continues across
   // words in the right order.
   RoutingFixture f(2, 128, 4);
   f.fillSource(0, 11.f);
   f.fillSource(1, 22.f);

   PlaybackOutputMask m;
   m.set(63);
   m.set(64);
   TrackChannelAssignment a;
   a.outputMask = m;

   RouteTrackSamples(a, 2, 128, f.samplesAvailable, unitVolume,
                     f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(62) == 0.f);
   REQUIRE(f.master(63) == 11.f);
   REQUIRE(f.master(64) == 22.f);
   REQUIRE(f.master(65) == 0.f);
}

TEST_CASE(
   "Hi-word bit masked off when device is narrower than 64 channels",
   "[RouteTrackSamples]")
{
   // Bit 64 is set but device only has 4 channels -> no audio.
   RoutingFixture f(1, 4, 4);
   f.fillSource(0, 2.f);

   RouteTrackSamples(WithMask(0, uint64_t(1)), 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   for (size_t ch = 0; ch < 4; ++ch)
      REQUIRE(f.master(ch) == 0.f);
}
