/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRoutingTests.cpp

  Tests for ComputeChannelAssignments -- the routing decision logic
  that determines which output channel each playback track goes to.

**********************************************************************/

#include "au3-mixer/ChannelRouting.h"

#include <gtest/gtest.h>

// ============================================================
// STEREO OUTPUT: Everything gets legacy behavior (-1)
// ============================================================

TEST(ChannelRouting, StereoOutput_AllTracksGetLegacy)
{
   // 4 mono tracks, stereo output
   auto assignments = ComputeChannelAssignments({1, 1, 1, 1}, 2);

   ASSERT_EQ(assignments.size(), 4u);
   for (auto& a : assignments)
      EXPECT_EQ(a.outputChannel, -1);
}

TEST(ChannelRouting, MonoOutput_AllTracksGetLegacy)
{
   auto assignments = ComputeChannelAssignments({1, 1}, 1);

   ASSERT_EQ(assignments.size(), 2u);
   for (auto& a : assignments)
      EXPECT_EQ(a.outputChannel, -1);
}

TEST(ChannelRouting, StereoOutput_StereoTrack_Legacy)
{
   auto assignments = ComputeChannelAssignments({2}, 2);

   ASSERT_EQ(assignments.size(), 1u);
   EXPECT_EQ(assignments[0].outputChannel, -1);
}

// ============================================================
// MULTI-CHANNEL OUTPUT: Identity routing for mono tracks
// ============================================================

TEST(ChannelRouting, SixOutput_SixMonoTracks_IdentityRouting)
{
   auto assignments = ComputeChannelAssignments(
      {1, 1, 1, 1, 1, 1}, 6);

   ASSERT_EQ(assignments.size(), 6u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
   EXPECT_EQ(assignments[1].outputChannel, 1);
   EXPECT_EQ(assignments[2].outputChannel, 2);
   EXPECT_EQ(assignments[3].outputChannel, 3);
   EXPECT_EQ(assignments[4].outputChannel, 4);
   EXPECT_EQ(assignments[5].outputChannel, 5);
}

TEST(ChannelRouting, SixteenOutput_SixteenMonoTracks_IdentityRouting)
{
   std::vector<size_t> tracks(16, 1);  // 16 mono tracks
   auto assignments = ComputeChannelAssignments(tracks, 16);

   ASSERT_EQ(assignments.size(), 16u);
   for (size_t i = 0; i < 16; ++i)
      EXPECT_EQ(assignments[i].outputChannel, static_cast<int>(i))
         << "Track " << i;
}

// ============================================================
// MULTI-CHANNEL: More tracks than outputs -> extras get legacy
// ============================================================

TEST(ChannelRouting, FourOutput_SixMonoTracks_ExtrasGetLegacy)
{
   auto assignments = ComputeChannelAssignments(
      {1, 1, 1, 1, 1, 1}, 4);

   ASSERT_EQ(assignments.size(), 6u);
   // First 4 get identity routing
   EXPECT_EQ(assignments[0].outputChannel, 0);
   EXPECT_EQ(assignments[1].outputChannel, 1);
   EXPECT_EQ(assignments[2].outputChannel, 2);
   EXPECT_EQ(assignments[3].outputChannel, 3);
   // Tracks beyond output count get legacy (duplicate to all)
   EXPECT_EQ(assignments[4].outputChannel, -1);
   EXPECT_EQ(assignments[5].outputChannel, -1);
}

// ============================================================
// MULTI-CHANNEL: Fewer tracks than outputs
// ============================================================

TEST(ChannelRouting, SixOutput_ThreeMonoTracks_IdentityRouting)
{
   auto assignments = ComputeChannelAssignments({1, 1, 1}, 6);

   ASSERT_EQ(assignments.size(), 3u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
   EXPECT_EQ(assignments[1].outputChannel, 1);
   EXPECT_EQ(assignments[2].outputChannel, 2);
   // Outputs 3-5 will be silent (no tracks assigned to them)
}

// ============================================================
// MULTI-CHANNEL: Mixed track widths
// ============================================================

TEST(ChannelRouting, SixOutput_MixedTracks_StereoThenMono)
{
   // 1 stereo track (2ch) + 4 mono tracks = 6 source channels
   auto assignments = ComputeChannelAssignments(
      {2, 1, 1, 1, 1}, 6);

   ASSERT_EQ(assignments.size(), 5u);
   // Stereo track: starts at output 0 (occupies 0 and 1)
   EXPECT_EQ(assignments[0].outputChannel, 0);
   // Mono tracks continue from where stereo left off
   EXPECT_EQ(assignments[1].outputChannel, 2);
   EXPECT_EQ(assignments[2].outputChannel, 3);
   EXPECT_EQ(assignments[3].outputChannel, 4);
   EXPECT_EQ(assignments[4].outputChannel, 5);
}

TEST(ChannelRouting, EightOutput_StereoAndMonoMix)
{
   // stereo + mono + stereo + mono + mono + mono = 2+1+2+1+1+1 = 8 channels
   auto assignments = ComputeChannelAssignments(
      {2, 1, 2, 1, 1, 1}, 8);

   ASSERT_EQ(assignments.size(), 6u);
   EXPECT_EQ(assignments[0].outputChannel, 0);  // stereo: 0-1
   EXPECT_EQ(assignments[1].outputChannel, 2);  // mono: 2
   EXPECT_EQ(assignments[2].outputChannel, 3);  // stereo: 3-4
   EXPECT_EQ(assignments[3].outputChannel, 5);  // mono: 5
   EXPECT_EQ(assignments[4].outputChannel, 6);  // mono: 6
   EXPECT_EQ(assignments[5].outputChannel, 7);  // mono: 7
}

// ============================================================
// MULTI-CHANNEL: Track overflows output range
// ============================================================

TEST(ChannelRouting, SixOutput_StereoAtEnd_OverflowsToLegacy)
{
   // 4 mono + 1 stereo = channels 0-3, then stereo needs 4-5
   auto assignments = ComputeChannelAssignments(
      {1, 1, 1, 1, 2}, 6);

   ASSERT_EQ(assignments.size(), 5u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
   EXPECT_EQ(assignments[1].outputChannel, 1);
   EXPECT_EQ(assignments[2].outputChannel, 2);
   EXPECT_EQ(assignments[3].outputChannel, 3);
   // Stereo track at offset 4: needs outputs 4-5, fits within 6
   EXPECT_EQ(assignments[4].outputChannel, 4);
}

TEST(ChannelRouting, FourOutput_StereoAtEnd_OverflowsToLegacy)
{
   // 3 mono + 1 stereo = would need outputs 0-2, then 3-4
   // But only 4 outputs, so stereo at offset 3 needs 3-4 which exceeds range
   auto assignments = ComputeChannelAssignments(
      {1, 1, 1, 2}, 4);

   ASSERT_EQ(assignments.size(), 4u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
   EXPECT_EQ(assignments[1].outputChannel, 1);
   EXPECT_EQ(assignments[2].outputChannel, 2);
   // Stereo track needs 2 outputs starting at 3, but only 1 available
   EXPECT_EQ(assignments[3].outputChannel, -1);
}

// ============================================================
// EDGE CASES
// ============================================================

TEST(ChannelRouting, EmptyTrackList)
{
   auto assignments = ComputeChannelAssignments({}, 6);
   EXPECT_TRUE(assignments.empty());
}

TEST(ChannelRouting, ZeroOutputChannels_AllLegacy)
{
   auto assignments = ComputeChannelAssignments({1, 1}, 0);

   ASSERT_EQ(assignments.size(), 2u);
   EXPECT_EQ(assignments[0].outputChannel, -1);
   EXPECT_EQ(assignments[1].outputChannel, -1);
}

// ============================================================
// OVERFLOW CASCADE: Once overflow occurs, all remaining get legacy
// to prevent mixing collisions
// ============================================================

TEST(ChannelRouting, OverflowCascade_MonoAfterOverflow_GetsLegacy)
{
   // stereo(2) + stereo(2) + mono(1) on 3 outputs
   // First stereo fits at 0 (next=2), second stereo doesn't fit (2+2>3)
   // -> overflow. Mono ALSO gets legacy (no collision).
   auto assignments = ComputeChannelAssignments({2, 2, 1}, 3);

   ASSERT_EQ(assignments.size(), 3u);
   EXPECT_EQ(assignments[0].outputChannel, 0);   // fits
   EXPECT_EQ(assignments[1].outputChannel, -1);   // overflow
   EXPECT_EQ(assignments[2].outputChannel, -1);   // cascade: also legacy
}

TEST(ChannelRouting, OverflowCascade_AllOverflow)
{
   auto assignments = ComputeChannelAssignments({4, 4}, 3);

   ASSERT_EQ(assignments.size(), 2u);
   EXPECT_EQ(assignments[0].outputChannel, -1);
   EXPECT_EQ(assignments[1].outputChannel, -1);
}

TEST(ChannelRouting, OverflowCascade_MultipleOverflows)
{
   auto assignments = ComputeChannelAssignments({2, 2, 2}, 3);

   ASSERT_EQ(assignments.size(), 3u);
   EXPECT_EQ(assignments[0].outputChannel, 0);   // fits
   EXPECT_EQ(assignments[1].outputChannel, -1);   // overflow
   EXPECT_EQ(assignments[2].outputChannel, -1);   // cascade
}

// ============================================================
// SINGLE MULTI-CHANNEL TRACK
// ============================================================

TEST(ChannelRouting, SingleSixChannelTrack_SixOutputs)
{
   auto assignments = ComputeChannelAssignments({6}, 6);

   ASSERT_EQ(assignments.size(), 1u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
}

TEST(ChannelRouting, SingleSixteenChannelTrack_SixteenOutputs)
{
   auto assignments = ComputeChannelAssignments({16}, 16);

   ASSERT_EQ(assignments.size(), 1u);
   EXPECT_EQ(assignments[0].outputChannel, 0);
}

TEST(ChannelRouting, SingleTrackLargerThanOutput_Legacy)
{
   auto assignments = ComputeChannelAssignments({8}, 4);

   ASSERT_EQ(assignments.size(), 1u);
   EXPECT_EQ(assignments[0].outputChannel, -1);
}

TEST(ChannelRouting, ZeroChannelTrack_Legacy)
{
   auto assignments = ComputeChannelAssignments({0}, 6);

   ASSERT_EQ(assignments.size(), 1u);
   // Zero-channel track gets legacy (overflow triggered by 0-channel check)
   EXPECT_EQ(assignments[0].outputChannel, -1);
}
