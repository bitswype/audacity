/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRoutingTest.cpp

  Unit tests for ComputeChannelAssignments after the 128-bit
  static-mask refactor.  The function is now a thin pass-through that
  wraps each per-track mask in a TrackChannelAssignment.  All former
  "auto routing" and "overflow" logic was moved out of this function
  (identity is materialized at track creation by the
  PlaybackRoutingListener in src/).

**********************************************************************/
#include "ChannelRouting.h"

#include <catch2/catch.hpp>

#include <vector>

TEST_CASE("ComputeChannelAssignments: empty input -> empty output",
   "[ChannelRouting]")
{
   const auto result = ComputeChannelAssignments({});
   REQUIRE(result.empty());
}

TEST_CASE("ComputeChannelAssignments: preserves mask count and contents",
   "[ChannelRouting]")
{
   std::vector<PlaybackOutputMask> masks {
      PlaybackOutputMask{ 0x1ull, 0 },
      PlaybackOutputMask{ 0x0ull, 0x4ull },
      PlaybackOutputMask{} // empty = silent
   };
   const auto result = ComputeChannelAssignments(masks);
   REQUIRE(result.size() == 3);
   REQUIRE(result[0].outputMask == PlaybackOutputMask{ 0x1ull, 0 });
   REQUIRE(result[1].outputMask == PlaybackOutputMask{ 0x0ull, 0x4ull });
   REQUIRE(result[2].outputMask.empty());
}

TEST_CASE("ComputeChannelAssignments: high-word bits pass through",
   "[ChannelRouting]")
{
   PlaybackOutputMask m;
   m.set(70);
   const auto result = ComputeChannelAssignments({ m });
   REQUIRE(result.size() == 1);
   REQUIRE(result[0].outputMask.test(70));
   REQUIRE(result[0].outputMask.lo == 0);
}
