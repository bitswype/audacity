/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  RouteTrackSamplesTest.cpp

  Tests for the sample-distribution loop extracted from
  AudioIO::ProcessPlaybackSlices.  Covers all five routing cases and
  the multi-channel mask distribution rules.

**********************************************************************/
#include "RouteTrackSamples.h"

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {

//! Fixture: samplesAvailable floats per buffer, zeroed on construction.
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

   //! Fill source channel @c ch with a constant value.
   void fillSource(size_t ch, float value)
   {
      std::fill(procStorage[ch].begin(), procStorage[ch].end(), value);
   }

   //! Master channel @c ch, first sample.
   float master(size_t ch, size_t i = 0) const
   {
      return masterStorage[ch][i];
   }

   //! Source channel @c ch, first sample (observes in-place mutation).
   float source(size_t ch, size_t i = 0) const
   {
      return procStorage[ch][i];
   }

   float* const* procBuffers() { return procPtrs.data(); }
   float* const* masterBuffers() { return masterPtrs.data(); }

   const size_t samplesAvailable;

private:
   std::vector<std::vector<float>> procStorage;
   std::vector<std::vector<float>> masterStorage;
   std::vector<float*> procPtrs;
   std::vector<float*> masterPtrs;
};

//! Return unit volume regardless of channel.
auto unitVolume = [](int) { return 1.f; };

} // namespace

TEST_CASE("Case 1: mask routes mono to a single output",
          "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 2.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0001;

   RouteTrackSamples(assignment, 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 2.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Case 1: mask duplicates mono to multiple outputs",
          "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 3.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0101; // outputs 0 and 2

   RouteTrackSamples(assignment, 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 3.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 3.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Case 1: mask distributes stereo across two outputs",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 4.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0011; // outputs 0, 1

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 1.f);
   REQUIRE(f.master(1) == 4.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Case 1: stereo with fewer mask bits than channels drops extra source",
          "[RouteTrackSamples]")
{
   // Mask has only one bit set but source is stereo -> only ch 0
   // routes, ch 1 is dropped entirely.
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 2.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0100; // only output 2

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 1.f); // only ch 0 lands
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Case 1: stereo with non-contiguous two-bit mask",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 4.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b1100; // outputs 2 and 3

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 1.f);
   REQUIRE(f.master(3) == 4.f);
}

TEST_CASE(
   "Case 1: 4-channel with split mask distributes sequentially",
   "[RouteTrackSamples]")
{
   // From the plan doc: "a 4-channel clip with mask 0b1100_0011 plays
   // its channel 0 on output 0, channel 1 on output 1, channel 2 on
   // output 6, channel 3 on output 7."
   RoutingFixture f(4, 8, 8);
   f.fillSource(0, 10.f);
   f.fillSource(1, 20.f);
   f.fillSource(2, 30.f);
   f.fillSource(3, 40.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b11000011;

   RouteTrackSamples(assignment, 4, 8, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 10.f);
   REQUIRE(f.master(1) == 20.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 0.f);
   REQUIRE(f.master(4) == 0.f);
   REQUIRE(f.master(5) == 0.f);
   REQUIRE(f.master(6) == 30.f);
   REQUIRE(f.master(7) == 40.f);
}

TEST_CASE("Case 1: stereo with more mask bits than channels clamps to last",
          "[RouteTrackSamples]")
{
   // Source has 2 channels, mask has 3 bits: ch0 -> bit0, ch1 -> bit1,
   // then srcChannel advances to 2 and is clamped to numChannels-1 = 1.
   // So bit2 also gets source ch 1.
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 5.f);

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0111; // outputs 0, 1, 2

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 1.f); // ch 0
   REQUIRE(f.master(1) == 5.f); // ch 1
   REQUIRE(f.master(2) == 5.f); // clamped to last (ch 1)
   REQUIRE(f.master(3) == 0.f);
}

TEST_CASE("Case 1: mask applies per-source-channel volume",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 1.f);

   // Volumes: ch 0 = 0.5, ch 1 = 2.0
   auto getVolume = [](int ch) { return ch == 0 ? 0.5f : 2.0f; };

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0011;

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == Catch::Detail::Approx(0.5f));
   REQUIRE(f.master(1) == Catch::Detail::Approx(2.0f));
}

TEST_CASE("Case 1: mask does not mutate processingBuffers",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 2.f);

   auto getVolume = [](int) { return 4.f; };

   TrackChannelAssignment assignment;
   assignment.outputMask = 0b0011;

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   // Master accumulates with volume applied, but source is untouched.
   REQUIRE(f.master(0) == 4.f);
   REQUIRE(f.master(1) == 8.f);
   REQUIRE(f.source(0) == 1.f);
   REQUIRE(f.source(1) == 2.f);
}

TEST_CASE("Case 2: multi-channel with assigned output uses identity",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 5, 8);
   f.fillSource(0, 3.f);
   f.fillSource(1, 4.f);

   auto getVolume = [](int) { return 2.f; };

   TrackChannelAssignment assignment;
   assignment.outputChannel = 2;
   assignment.outputMask = 0;

   RouteTrackSamples(assignment, 2, 5, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 6.f);
   REQUIRE(f.master(3) == 8.f);
   REQUIRE(f.master(4) == 0.f);
}

TEST_CASE("Case 2: mutates processingBuffers in place (historical quirk)",
          "[RouteTrackSamples]")
{
   // Case 2 applies volume to the source buffers *= in place.  This
   // is a side effect the existing engine relies on for downstream
   // stages that re-read from mProcessingBuffers.
   RoutingFixture f(2, 4, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 1.f);

   auto getVolume = [](int ch) { return ch == 0 ? 0.25f : 0.5f; };

   TrackChannelAssignment assignment;
   assignment.outputChannel = 0;

   RouteTrackSamples(assignment, 2, 4, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.source(0) == Catch::Detail::Approx(0.25f));
   REQUIRE(f.source(1) == Catch::Detail::Approx(0.50f));
}

TEST_CASE("Case 2: overflow truncates at numOutputChannels",
          "[RouteTrackSamples]")
{
   // 2-channel track assigned to start at output 4 on a 5-output
   // device.  cnt = min(2, 5-4) = 1.  Only ch 0 is routed.
   RoutingFixture f(2, 5, 8);
   f.fillSource(0, 7.f);
   f.fillSource(1, 9.f);

   TrackChannelAssignment assignment;
   assignment.outputChannel = 4;

   RouteTrackSamples(assignment, 2, 5, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(4) == 7.f);
   // ch 1 is not routed anywhere; its source buffer is not mutated
   // because cnt=1 excludes it from the n<cnt loop.
   REQUIRE(f.source(1) == 9.f);
}

TEST_CASE("Case 3: multi-channel legacy routing is identity from channel 0",
          "[RouteTrackSamples]")
{
   RoutingFixture f(2, 2, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 2.f);

   auto getVolume = [](int) { return 3.f; };

   TrackChannelAssignment assignment;
   assignment.outputChannel = -1;

   RouteTrackSamples(assignment, 2, 2, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 3.f);
   REQUIRE(f.master(1) == 6.f);
}

TEST_CASE("Case 3: multi-channel legacy truncates excess source channels",
          "[RouteTrackSamples]")
{
   // 4-channel source, 2 outputs -> only ch 0..1 are routed; ch 2..3
   // sit in processingBuffers untouched (not mutated by the helper
   // since cnt=2 bounds the loop).
   RoutingFixture f(4, 2, 8);
   f.fillSource(0, 1.f);
   f.fillSource(1, 2.f);
   f.fillSource(2, 3.f);
   f.fillSource(3, 4.f);

   TrackChannelAssignment assignment;
   assignment.outputChannel = -1;

   RouteTrackSamples(assignment, 4, 2, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 1.f);
   REQUIRE(f.master(1) == 2.f);
   REQUIRE(f.source(2) == 3.f); // untouched
   REQUIRE(f.source(3) == 4.f); // untouched
}

TEST_CASE("Case 4: mono with assigned output sends to that channel only",
          "[RouteTrackSamples]")
{
   RoutingFixture f(1, 5, 8);
   f.fillSource(0, 2.f);

   auto getVolume = [](int ch) {
      // Only ch 0 is queried in case 4.
      return ch == 0 ? 3.f : -999.f;
   };

   TrackChannelAssignment assignment;
   assignment.outputChannel = 3;

   RouteTrackSamples(assignment, 1, 5, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 6.f);
   REQUIRE(f.master(4) == 0.f);
   // Source not mutated.
   REQUIRE(f.source(0) == 2.f);
}

TEST_CASE("Case 5: mono legacy duplicates source to every output",
          "[RouteTrackSamples]")
{
   RoutingFixture f(1, 4, 8);
   f.fillSource(0, 5.f);

   // Case 5 is the one case where getChannelVolume is indexed by the
   // *output* channel, not the source channel.  Volume 1, 2, 3, 4 per
   // output.
   auto getVolume = [](int outCh) {
      return static_cast<float>(outCh + 1);
   };

   TrackChannelAssignment assignment;
   assignment.outputChannel = -1;

   RouteTrackSamples(assignment, 1, 4, f.samplesAvailable,
                     getVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 5.f);  // 5 * 1
   REQUIRE(f.master(1) == 10.f); // 5 * 2
   REQUIRE(f.master(2) == 15.f); // 5 * 3
   REQUIRE(f.master(3) == 20.f); // 5 * 4
   // Source not mutated.
   REQUIRE(f.source(0) == 5.f);
}

TEST_CASE("Multiple tracks summed into the same output",
          "[RouteTrackSamples]")
{
   // Two mono tracks both routed to output 0.  AudioIO calls the
   // helper once per track; masters accumulate.
   RoutingFixture f1(1, 2, 4);
   f1.fillSource(0, 1.f);
   RoutingFixture f2(1, 2, 4);
   f2.fillSource(0, 3.f);

   std::vector<float> master0(4, 0.f);
   std::vector<float> master1(4, 0.f);
   std::array<float*, 2> masters = { master0.data(), master1.data() };

   TrackChannelAssignment a1;
   a1.outputMask = 0b01;
   TrackChannelAssignment a2;
   a2.outputMask = 0b01;

   RouteTrackSamples(a1, 1, 2, 4, unitVolume,
                     f1.procBuffers(), masters.data());
   RouteTrackSamples(a2, 1, 2, 4, unitVolume,
                     f2.procBuffers(), masters.data());

   REQUIRE(master0[0] == 4.f); // 1 + 3
   REQUIRE(master1[0] == 0.f);
}

TEST_CASE("Default-constructed assignment is case 5 for mono",
          "[RouteTrackSamples]")
{
   // outputMask = 0, outputChannel = -1 (default).  Mono source ->
   // legacy duplication.  This is the "no routing configured"
   // baseline that preserved pre-refactor behavior.
   RoutingFixture f(1, 2, 4);
   f.fillSource(0, 7.f);

   TrackChannelAssignment assignment{}; // defaults

   RouteTrackSamples(assignment, 1, 2, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 7.f);
   REQUIRE(f.master(1) == 7.f);
}

TEST_CASE("Default-constructed assignment is case 3 for stereo",
          "[RouteTrackSamples]")
{
   // Stereo source, no routing -> legacy stereo.
   RoutingFixture f(2, 2, 4);
   f.fillSource(0, 2.f);
   f.fillSource(1, 3.f);

   TrackChannelAssignment assignment{};

   RouteTrackSamples(assignment, 2, 2, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 2.f);
   REQUIRE(f.master(1) == 3.f);
}

TEST_CASE("Mask takes priority over outputChannel when both are set",
          "[RouteTrackSamples]")
{
   // If a consumer accidentally populates both fields, the mask wins
   // (this is the documented precedence rule).
   RoutingFixture f(1, 4, 4);
   f.fillSource(0, 6.f);

   TrackChannelAssignment assignment;
   assignment.outputChannel = 0;    // would say "output 0 only"
   assignment.outputMask = 0b1000;  // mask wins -> output 3 only

   RouteTrackSamples(assignment, 1, 4, f.samplesAvailable,
                     unitVolume, f.procBuffers(), f.masterBuffers());

   REQUIRE(f.master(0) == 0.f);
   REQUIRE(f.master(1) == 0.f);
   REQUIRE(f.master(2) == 0.f);
   REQUIRE(f.master(3) == 6.f);
}
