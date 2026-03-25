/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  MixOutputTests.cpp

  Integration tests for MixToOutputBuffers -- the extracted mixing
  logic that routes per-track audio into output channel buffers.

  These tests verify actual audio data flow, not just metadata/routing.

**********************************************************************/

#include "au3-mixer/MixOutput.h"
#include "au3-mixer/ChannelRouting.h"

#include <gtest/gtest.h>
#include <cmath>
#include <numeric>

namespace {

// Create a vector of per-channel buffers with DC values
std::vector<std::vector<float>> MakeDCBuffers(
   const std::vector<float>& dcPerChannel, size_t numSamples)
{
   std::vector<std::vector<float>> result;
   for (float dc : dcPerChannel)
      result.push_back(std::vector<float>(numSamples, dc));
   return result;
}

// Create zero-filled master buffers
std::vector<std::vector<float>> MakeMasterBuffers(
   size_t numChannels, size_t numSamples)
{
   return std::vector<std::vector<float>>(
      numChannels, std::vector<float>(numSamples, 0.0f));
}

// Unity gain for all channels
float UnityGain(size_t, size_t) { return 1.0f; }

// Check that all samples in a buffer are approximately equal to expected
void ExpectDC(const std::vector<float>& buf, float expected,
              const char* label, float tol = 1e-6f)
{
   for (size_t i = 0; i < buf.size(); ++i)
      EXPECT_NEAR(buf[i], expected, tol)
         << label << " sample " << i;
}

// Check that a buffer is all zeros
void ExpectSilence(const std::vector<float>& buf, const char* label)
{
   ExpectDC(buf, 0.0f, label);
}

} // namespace

// ============================================================
// MONO TRACK WITH ASSIGNED OUTPUT (Case 3 in ProcessPlaybackSlices)
// ============================================================

TEST(MixOutput, MonoTrack_AssignedToOutput0_AppearsOnOutput0Only)
{
   // 1 mono track, DC=0.5, assigned to output 0
   auto proc = MakeDCBuffers({0.5f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{0}};
   std::vector<size_t> channelCounts = {1};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectDC(master[0], 0.5f, "output 0");
   ExpectSilence(master[1], "output 1");
   ExpectSilence(master[2], "output 2");
   ExpectSilence(master[3], "output 3");
}

TEST(MixOutput, MonoTrack_AssignedToOutput3_AppearsOnOutput3Only)
{
   auto proc = MakeDCBuffers({0.7f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{3}};
   std::vector<size_t> channelCounts = {1};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectSilence(master[0], "output 0");
   ExpectSilence(master[1], "output 1");
   ExpectSilence(master[2], "output 2");
   ExpectDC(master[3], 0.7f, "output 3");
}

// ============================================================
// MONO TRACK WITH LEGACY ROUTING (Case 4)
// ============================================================

TEST(MixOutput, MonoTrack_Legacy_DuplicatesToAllOutputs)
{
   auto proc = MakeDCBuffers({0.3f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{-1}};
   std::vector<size_t> channelCounts = {1};

   // Legacy: each output gets gain(trackIdx, outputChannel) * source
   auto gain = [](size_t, size_t ch) -> float {
      return (ch == 0) ? 1.0f : 0.5f;  // only output 0 gets full gain
   };

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, gain);

   ExpectDC(master[0], 0.3f, "output 0");       // 0.3 * 1.0
   ExpectDC(master[1], 0.15f, "output 1");       // 0.3 * 0.5
   ExpectDC(master[2], 0.15f, "output 2");       // 0.3 * 0.5
   ExpectDC(master[3], 0.15f, "output 3");       // 0.3 * 0.5
}

// ============================================================
// MULTI-CHANNEL TRACK WITH ASSIGNED OUTPUT (Case 1)
// ============================================================

TEST(MixOutput, StereoTrack_AssignedToOutput0_IdentityRouting)
{
   // Stereo track: ch0=0.4, ch1=0.8, assigned to output 0 (occupies 0-1)
   auto proc = MakeDCBuffers({0.4f, 0.8f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{0}};
   std::vector<size_t> channelCounts = {2};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectDC(master[0], 0.4f, "output 0");
   ExpectDC(master[1], 0.8f, "output 1");
   ExpectSilence(master[2], "output 2");
   ExpectSilence(master[3], "output 3");
}

TEST(MixOutput, StereoTrack_AssignedToOutput2_IdentityRouting)
{
   auto proc = MakeDCBuffers({0.1f, 0.9f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{2}};
   std::vector<size_t> channelCounts = {2};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectSilence(master[0], "output 0");
   ExpectSilence(master[1], "output 1");
   ExpectDC(master[2], 0.1f, "output 2");
   ExpectDC(master[3], 0.9f, "output 3");
}

// ============================================================
// MULTIPLE TRACKS -- the real integration scenario
// ============================================================

TEST(MixOutput, FourMonoTracks_FourOutputs_IdentityRouting)
{
   // Track 0: DC=0.1, Track 1: DC=0.2, Track 2: DC=0.3, Track 3: DC=0.4
   auto proc = MakeDCBuffers({0.1f, 0.2f, 0.3f, 0.4f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   auto assignments = ComputeChannelAssignments({1, 1, 1, 1}, 4);
   std::vector<size_t> channelCounts = {1, 1, 1, 1};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectDC(master[0], 0.1f, "output 0");
   ExpectDC(master[1], 0.2f, "output 1");
   ExpectDC(master[2], 0.3f, "output 2");
   ExpectDC(master[3], 0.4f, "output 3");
}

TEST(MixOutput, SixteenMonoTracks_SixteenOutputs_IdentityRouting)
{
   std::vector<float> dcValues(16);
   for (size_t i = 0; i < 16; ++i)
      dcValues[i] = (i + 1) * 0.05f;

   auto proc = MakeDCBuffers(dcValues, 64);
   auto master = MakeMasterBuffers(16, 64);

   std::vector<size_t> channelCounts(16, 1);
   auto assignments = ComputeChannelAssignments(channelCounts, 16);

   MixToOutputBuffers(proc, master, assignments, channelCounts, 16, 64, UnityGain);

   for (size_t ch = 0; ch < 16; ++ch) {
      float expected = (ch + 1) * 0.05f;
      ExpectDC(master[ch], expected, ("output " + std::to_string(ch)).c_str());
   }
}

TEST(MixOutput, MixedTracks_StereoThenMono_CorrectRouting)
{
   // 1 stereo (ch0=0.1, ch1=0.2) + 2 mono (0.3, 0.4) on 4 outputs
   auto proc = MakeDCBuffers({0.1f, 0.2f, 0.3f, 0.4f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<size_t> channelCounts = {2, 1, 1};
   auto assignments = ComputeChannelAssignments(channelCounts, 4);
   // Expected: stereo at 0-1, mono at 2, mono at 3

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   ExpectDC(master[0], 0.1f, "output 0 (stereo L)");
   ExpectDC(master[1], 0.2f, "output 1 (stereo R)");
   ExpectDC(master[2], 0.3f, "output 2 (mono)");
   ExpectDC(master[3], 0.4f, "output 3 (mono)");
}

// ============================================================
// GAIN APPLICATION
// ============================================================

TEST(MixOutput, MonoTrack_GainApplied)
{
   auto proc = MakeDCBuffers({1.0f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{2}};
   std::vector<size_t> channelCounts = {1};

   auto halfGain = [](size_t, size_t) -> float { return 0.5f; };

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, halfGain);

   ExpectDC(master[2], 0.5f, "output 2 with 0.5 gain");
}

TEST(MixOutput, StereoTrack_PerChannelGain)
{
   auto proc = MakeDCBuffers({1.0f, 1.0f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{0}};
   std::vector<size_t> channelCounts = {2};

   auto gain = [](size_t, size_t ch) -> float {
      return (ch == 0) ? 0.75f : 0.25f;
   };

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, gain);

   ExpectDC(master[0], 0.75f, "output 0");
   ExpectDC(master[1], 0.25f, "output 1");
}

// ============================================================
// ACCUMULATION: Two tracks sum on the same output
// ============================================================

TEST(MixOutput, TwoMonoTracks_Legacy_SumOnAllOutputs)
{
   // Two mono tracks, both legacy (-1), both DC=0.5
   auto proc = MakeDCBuffers({0.5f, 0.5f}, 64);
   auto master = MakeMasterBuffers(2, 64);

   std::vector<TrackChannelAssignment> assignments = {{-1}, {-1}};
   std::vector<size_t> channelCounts = {1, 1};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 2, 64, UnityGain);

   // Both tracks duplicate to both outputs: 0.5 + 0.5 = 1.0
   ExpectDC(master[0], 1.0f, "output 0 (sum)");
   ExpectDC(master[1], 1.0f, "output 1 (sum)");
}

// ============================================================
// EDGE CASES
// ============================================================

TEST(MixOutput, EmptyTrackList_OutputsStayZero)
{
   auto master = MakeMasterBuffers(4, 64);

   std::vector<std::vector<float>> proc;
   std::vector<TrackChannelAssignment> assignments;
   std::vector<size_t> channelCounts;

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   for (size_t ch = 0; ch < 4; ++ch)
      ExpectSilence(master[ch], ("output " + std::to_string(ch)).c_str());
}

TEST(MixOutput, ZeroSamples_NoCrash)
{
   auto proc = MakeDCBuffers({0.5f}, 0);
   auto master = MakeMasterBuffers(4, 0);

   std::vector<TrackChannelAssignment> assignments = {{0}};
   std::vector<size_t> channelCounts = {1};

   // Should not crash
   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 0, UnityGain);
}

// ============================================================
// MULTI-CHANNEL TRACK WITH LEGACY ROUTING (Case 2)
// ============================================================

TEST(MixOutput, StereoTrack_Legacy_OnlyTouchesFirstTwoOutputs)
{
   // Stereo track with legacy routing on a 6-output device.
   // Should route L->out0, R->out1; outputs 2-5 must stay silent.
   auto proc = MakeDCBuffers({0.7f, 0.3f}, 64);
   auto master = MakeMasterBuffers(6, 64);

   std::vector<TrackChannelAssignment> assignments = {{-1}};
   std::vector<size_t> channelCounts = {2};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 6, 64, UnityGain);

   ExpectDC(master[0], 0.7f, "output 0 (left)");
   ExpectDC(master[1], 0.3f, "output 1 (right)");
   for (size_t ch = 2; ch < 6; ++ch)
      ExpectSilence(master[ch], ("output " + std::to_string(ch)).c_str());
}

TEST(MixOutput, TwoStereoTracks_Legacy_Accumulate)
{
   // Two stereo tracks, both legacy, accumulate on outputs 0 and 1.
   auto proc = MakeDCBuffers({0.5f, 0.4f, 0.3f, 0.2f}, 64);
   auto master = MakeMasterBuffers(4, 64);

   std::vector<TrackChannelAssignment> assignments = {{-1}, {-1}};
   std::vector<size_t> channelCounts = {2, 2};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 4, 64, UnityGain);

   // Track 1: L=0.5 R=0.4, Track 2: L=0.3 R=0.2
   ExpectDC(master[0], 0.8f, "output 0 (left sum)");
   ExpectDC(master[1], 0.6f, "output 1 (right sum)");
   ExpectSilence(master[2], "output 2");
   ExpectSilence(master[3], "output 3");
}

TEST(MixOutput, SixChannelTrack_Legacy_IdentityRoutes)
{
   // 6-channel track with legacy routing on 6-output device.
   // Case 2: each source channel n routes to master[n].
   auto proc = MakeDCBuffers({0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}, 64);
   auto master = MakeMasterBuffers(6, 64);

   std::vector<TrackChannelAssignment> assignments = {{-1}};
   std::vector<size_t> channelCounts = {6};

   MixToOutputBuffers(proc, master, assignments, channelCounts, 6, 64, UnityGain);

   for (size_t ch = 0; ch < 6; ++ch) {
      ExpectDC(master[ch], 0.1f * (ch + 1),
         ("output " + std::to_string(ch)).c_str());
   }
}
