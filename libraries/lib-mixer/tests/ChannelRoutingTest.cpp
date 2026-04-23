/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRoutingTest.cpp

  Unit tests for ComputeChannelAssignments, the pure function that turns
  a set of playback tracks into per-track output channel assignments for
  the N-channel playback routing feature (Audacity-MC fork).

**********************************************************************/
#include "ChannelRouting.h"

#include <catch2/catch.hpp>

#include <cstdint>
#include <vector>

namespace
{
//! Shorthand for the common "no explicit masks" call.
std::vector<TrackChannelAssignment>
Compute(const std::vector<size_t>& counts, size_t numOutputs)
{
   return ComputeChannelAssignments(counts, {}, numOutputs);
}
} // namespace

TEST_CASE("ComputeChannelAssignments: empty inputs", "[ChannelRouting]")
{
   // An empty track list must produce an empty assignment list regardless of
   // the number of output channels.
   const auto result = Compute({}, 4);
   REQUIRE(result.empty());
}

TEST_CASE(
   "ComputeChannelAssignments: single mono track, 2 outputs -> legacy (-1)",
   "[ChannelRouting]")
{
   // With <=2 output channels, auto-routed tracks fall back to the legacy
   // stereo duplication path (outputChannel == -1).
   const auto result = Compute({ 1 }, 2);
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputChannel == -1);
   REQUIRE(result[0].outputMask == 0);
}

TEST_CASE(
   "ComputeChannelAssignments: single mono track, 1 output -> legacy (-1)",
   "[ChannelRouting]")
{
   const auto result = Compute({ 1 }, 1);
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputChannel == -1);
   REQUIRE(result[0].outputMask == 0);
}

TEST_CASE(
   "ComputeChannelAssignments: single stereo track, 2 outputs -> legacy (-1)",
   "[ChannelRouting]")
{
   const auto result = Compute({ 2 }, 2);
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputChannel == -1);
   REQUIRE(result[0].outputMask == 0);
}

TEST_CASE(
   "ComputeChannelAssignments: three mono tracks, 4 outputs, no masks -> identity 0,1,2",
   "[ChannelRouting]")
{
   // Multi-channel output: auto-routed mono tracks are assigned sequential
   // output channels starting from 0.
   const auto result = Compute({ 1, 1, 1 }, 4);
   REQUIRE(result.size() == 3);
   REQUIRE(result[0].outputChannel == 0);
   REQUIRE(result[1].outputChannel == 1);
   REQUIRE(result[2].outputChannel == 2);
   for (const auto& a : result)
      REQUIRE(a.outputMask == 0);
}

TEST_CASE(
   "ComputeChannelAssignments: stereo+mono, 4 outputs -> stereo=0, mono=2",
   "[ChannelRouting]")
{
   // A stereo track consumes two output channels; the next auto-routed
   // track must start at channel 2.
   const auto result = Compute({ 2, 1 }, 4);
   REQUIRE(result.size() == 2);
   REQUIRE(result[0].outputChannel == 0);
   REQUIRE(result[1].outputChannel == 2);
}

TEST_CASE(
   "ComputeChannelAssignments: three mono tracks, 2 outputs -> all legacy (-1)",
   "[ChannelRouting]")
{
   // numOutputChannels<=2 forces legacy behavior on all auto-routed tracks
   // (i.e. identity routing is not applied even if there are more tracks
   // than output channels).
   const auto result = Compute({ 1, 1, 1 }, 2);
   REQUIRE(result.size() == 3);
   for (const auto& a : result) {
      REQUIRE(a.outputChannel == -1);
      REQUIRE(a.outputMask == 0);
   }
}

TEST_CASE(
   "ComputeChannelAssignments: overflow infects subsequent tracks",
   "[ChannelRouting]")
{
   // Five mono tracks, 4 outputs:
   // tracks 0..3 fit into outputs 0..3, then track 4 overflows.
   // Verify the overflow flag infects all remaining tracks (so a small
   // track after an oversized one also falls back to -1) by inserting a
   // large track followed by a small track.
   SECTION("plain overflow at end")
   {
      const auto result = Compute({ 1, 1, 1, 1, 1 }, 4);
      REQUIRE(result.size() == 5);
      REQUIRE(result[0].outputChannel == 0);
      REQUIRE(result[1].outputChannel == 1);
      REQUIRE(result[2].outputChannel == 2);
      REQUIRE(result[3].outputChannel == 3);
      REQUIRE(result[4].outputChannel == -1);
   }

   SECTION("oversized track triggers overflow for later tracks")
   {
      // Layout: [mono at 0], [5-channel track that does not fit in remaining
      // 3 channels -> triggers overflow], [mono that would fit at 1 but
      // must be -1 because overflow infects subsequent tracks].
      const auto result = Compute({ 1, 5, 1 }, 4);
      REQUIRE(result.size() == 3);
      REQUIRE(result[0].outputChannel == 0);
      REQUIRE(result[1].outputChannel == -1);
      REQUIRE(result[2].outputChannel == -1);
   }
}

TEST_CASE(
   "ComputeChannelAssignments: explicit mask on sole track preserved, outputChannel untouched",
   "[ChannelRouting]")
{
   // An explicit mask short-circuits all identity/overflow logic. Verify
   // the mask is passed through verbatim and outputChannel remains at its
   // default (-1).
   const std::vector<size_t> counts { 1 };
   const std::vector<uint64_t> masks { 0x1ull };
   const auto result = ComputeChannelAssignments(counts, masks, 4);
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputMask == 0x1ull);
   REQUIRE(result[0].outputChannel == -1);
}

TEST_CASE(
   "ComputeChannelAssignments: masked track skipped in auto counter",
   "[ChannelRouting]")
{
   // Track A is explicitly masked (outputMask = 0x4); the auto-routing
   // counter should not advance for A. Track B (unmasked, mono) should
   // therefore receive output channel 0 -- the first sequential channel --
   // NOT channel 2 (the bit owned by A) and NOT channel 1 (post-A advance).
   //
   // This is the documented rule: "Explicitly masked tracks are skipped
   // in the next-channel counter because the user picked their channels
   // manually."
   const std::vector<size_t> counts { 1, 1 };
   const std::vector<uint64_t> masks { 0x4ull, 0 };
   const auto result = ComputeChannelAssignments(counts, masks, 4);
   REQUIRE(result.size() == 2);

   // Track A: mask preserved, outputChannel untouched.
   REQUIRE(result[0].outputMask == 0x4ull);
   REQUIRE(result[0].outputChannel == -1);

   // Track B: no mask, gets the first auto channel.
   REQUIRE(result[1].outputMask == 0);
   REQUIRE(result[1].outputChannel == 0);
}

TEST_CASE(
   "ComputeChannelAssignments: trackOutputMasks shorter than counts",
   "[ChannelRouting]")
{
   // Missing entries in trackOutputMasks are treated as 0 (no explicit
   // mask). The function must not crash or misroute.
   const std::vector<size_t> counts { 1, 1, 1 };
   const std::vector<uint64_t> masks { 0x8ull }; // only first entry
   const auto result = ComputeChannelAssignments(counts, masks, 4);
   REQUIRE(result.size() == 3);

   REQUIRE(result[0].outputMask == 0x8ull);
   REQUIRE(result[0].outputChannel == -1);

   // The remaining two tracks are auto-routed starting at output 0,
   // because the masked track doesn't advance the counter.
   REQUIRE(result[1].outputMask == 0);
   REQUIRE(result[1].outputChannel == 0);
   REQUIRE(result[2].outputMask == 0);
   REQUIRE(result[2].outputChannel == 1);
}

TEST_CASE(
   "ComputeChannelAssignments: large mask (bit 63) preserved verbatim",
   "[ChannelRouting]")
{
   // The public contract is that outputMask is passed through byte-for-byte.
   // Exercise the top bit to catch any accidental sign-extension or narrowing.
   const uint64_t hugeMask = 1ull << 63;
   const std::vector<size_t> counts { 1 };
   const std::vector<uint64_t> masks { hugeMask };
   const auto result = ComputeChannelAssignments(counts, masks, 4);
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputMask == hugeMask);
   REQUIRE(result[0].outputChannel == -1);
}

TEST_CASE(
   "ComputeChannelAssignments: empty track (channelCount == 0) triggers overflow",
   "[ChannelRouting]")
{
   // A track with channelCount == 0 cannot fit anywhere; per the
   // implementation this sets outputChannel to -1 for this track and
   // flips the overflow flag so all subsequent auto-routed tracks also
   // fall back to -1.
   const std::vector<size_t> counts { 0, 1 };
   const auto result = Compute(counts, 4);
   REQUIRE(result.size() == 2);
   REQUIRE(result[0].outputChannel == -1);
   REQUIRE(result[1].outputChannel == -1);
}
