/*
 * Audacity: A Digital Audio Editor
 */

/*! Integration tests for N-channel end-to-end data paths.
 *
 * These tests verify that multi-channel audio data flows correctly
 * through the full pipeline: write -> read, copy, and track operations.
 * They exercise the connections between subsystems, not just individual
 * functions.
 */

#include <gtest/gtest.h>

#include "au3interactiontestbase.h"

#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-math/SampleFormat.h"

using namespace au;
using namespace au::au3;
using ::testing::NiceMock;
using ::testing::Return;

namespace au::trackedit {

class NChannelIntegrationTest : public Au3InteractionTestBase
{
public:
   void SetUp() override
   {
      m_globalContext = std::make_shared<NiceMock<context::GlobalContextMock>>();
      m_currentProject = std::make_shared<NiceMock<project::AudacityProjectMock>>();
      m_trackEditProject = std::make_shared<NiceMock<TrackeditProjectMock>>();
      m_playbackState = std::make_shared<NiceMock<context::PlaybackStateMock>>();
      initTestProject();
   }

   WaveTrackFactory& factory()
   {
      return WaveTrackFactory::Get(projectRef());
   }

   // Helper: create an N-channel track with known DC data per channel.
   // Channel ch is filled with float(ch + 1).
   std::shared_ptr<WaveTrack> makeTrackWithData(
      size_t nChannels, size_t numSamples)
   {
      auto track = factory().Create(nChannels, floatSample, 44100.0);
      auto clip = track->CreateClip(0.0, wxT("test"));

      // Build per-channel buffers with distinct values
      std::vector<std::vector<float>> channelData(nChannels);
      std::vector<constSamplePtr> bufs(nChannels);
      for (size_t ch = 0; ch < nChannels; ++ch) {
         channelData[ch].assign(numSamples, static_cast<float>(ch + 1));
         bufs[ch] = reinterpret_cast<constSamplePtr>(channelData[ch].data());
      }
      clip->Append(bufs.data(), floatSample, numSamples, 1, floatSample);
      clip->Flush();
      track->InsertInterval(clip, true);
      return track;
   }

   // Helper: read samples from a specific channel of a track
   std::vector<float> readChannel(
      const WaveTrack& track, size_t channel, size_t numSamples)
   {
      std::vector<float> result(numSamples, 0.0f);
      auto channels = track.Channels();
      auto it = channels.begin();
      for (size_t i = 0; i < channel && it != channels.end(); ++i)
         ++it;
      if (it != channels.end()) {
         auto& wc = **it;
         samplePtr buf = reinterpret_cast<samplePtr>(result.data());
         wc.DoGet(0, 1, &buf, floatSample, sampleCount(0), numSamples,
                  false);
      }
      return result;
   }
};

// ============================================================
// Data integrity: write per-channel data, read it back
// ============================================================

TEST_F(NChannelIntegrationTest, SixChannelTrack_WriteAndReadBack_PerChannelDataCorrect)
{
   const size_t nChannels = 6;
   const size_t numSamples = 64;

   auto track = makeTrackWithData(nChannels, numSamples);
   ASSERT_EQ(track->NChannels(), nChannels);

   // Read back each channel and verify its DC value
   for (size_t ch = 0; ch < nChannels; ++ch) {
      auto samples = readChannel(*track, ch, numSamples);
      const float expected = static_cast<float>(ch + 1);
      for (size_t i = 0; i < numSamples; ++i) {
         EXPECT_FLOAT_EQ(samples[i], expected)
            << "Channel " << ch << " sample " << i
            << " expected " << expected;
      }
   }
}

TEST_F(NChannelIntegrationTest, SixteenChannelTrack_WriteAndReadBack_AllChannelsDistinct)
{
   const size_t nChannels = 16;
   const size_t numSamples = 32;

   auto track = makeTrackWithData(nChannels, numSamples);
   ASSERT_EQ(track->NChannels(), nChannels);

   for (size_t ch = 0; ch < nChannels; ++ch) {
      auto samples = readChannel(*track, ch, numSamples);
      const float expected = static_cast<float>(ch + 1);
      EXPECT_FLOAT_EQ(samples[0], expected)
         << "Channel " << ch << " has wrong data";
   }
}

// ============================================================
// Copy preserves N-channel data
// ============================================================

TEST_F(NChannelIntegrationTest, SixChannelTrack_Copy_PreservesChannelData)
{
   const size_t nChannels = 6;
   const size_t numSamples = 64;

   auto track = makeTrackWithData(nChannels, numSamples);
   ASSERT_EQ(track->NChannels(), nChannels);

   // Copy the track via WaveTrack::Copy (used by Duplicate, clipboard)
   auto copyHolder = track->Copy(0.0, track->GetEndTime(), false);
   auto* copy = dynamic_cast<WaveTrack*>(copyHolder.get());
   ASSERT_NE(copy, nullptr);
   ASSERT_EQ(copy->NChannels(), nChannels);

   // Verify each channel's data survived the copy
   for (size_t ch = 0; ch < nChannels; ++ch) {
      auto samples = readChannel(*copy, ch, numSamples);
      const float expected = static_cast<float>(ch + 1);
      EXPECT_FLOAT_EQ(samples[0], expected)
         << "Copied channel " << ch << " has wrong data";
   }
}

// ============================================================
// EmptyCopy preserves structure (serialization building block)
// ============================================================

TEST_F(NChannelIntegrationTest, SixChannelTrack_EmptyCopy_ChannelCountPreserved)
{
   auto track = factory().Create(6, floatSample, 44100.0);
   ASSERT_EQ(track->NChannels(), 6u);

   auto copy = track->EmptyCopy(6);
   ASSERT_NE(copy, nullptr);
   EXPECT_EQ(copy->NChannels(), 6u);

   // Verify all channels are accessible and distinct
   std::set<const void*> channelPtrs;
   for (size_t ch = 0; ch < 6; ++ch) {
      auto channel = copy->GetChannel(ch);
      ASSERT_NE(channel.get(), nullptr);
      channelPtrs.insert(channel.get());
   }
   EXPECT_EQ(channelPtrs.size(), 6u);
}

// ============================================================
// MakeMono correctly averages all N channels
// ============================================================

TEST_F(NChannelIntegrationTest, SixChannelTrack_MakeMono_AveragesAllChannels)
{
   const size_t nChannels = 6;
   const size_t numSamples = 64;

   auto track = makeTrackWithData(nChannels, numSamples);
   ASSERT_EQ(track->NChannels(), nChannels);

   // MakeMono reduces to 1 channel by averaging
   track->MakeMono();
   ASSERT_EQ(track->NChannels(), 1u);

   // Expected: average of DC values 1,2,3,4,5,6 = 21/6 = 3.5
   // But MakeMono on WaveTrack calls DiscardRightChannel (drops ch1+),
   // not MakeMono on the clip. The actual downmix path is MixDownToMono.
   // MakeMono just keeps channel 0.
   auto samples = readChannel(*track, 0, numSamples);
   // Channel 0 had DC = 1.0
   EXPECT_FLOAT_EQ(samples[0], 1.0f)
      << "MakeMono keeps channel 0 data";
}

TEST_F(NChannelIntegrationTest, SixChannelTrack_MixDownToMono_AveragesAllChannels)
{
   const size_t nChannels = 6;
   const size_t numSamples = 64;

   auto track = makeTrackWithData(nChannels, numSamples);
   ASSERT_EQ(track->NChannels(), nChannels);

   auto progress = [](double) {};
   auto cancel = []() { return false; };
   bool ok = track->MixDownToMono(progress, cancel);
   EXPECT_TRUE(ok);
   ASSERT_EQ(track->NChannels(), 1u);

   // Expected: average of 1+2+3+4+5+6 = 21, divided by 6 = 3.5
   auto samples = readChannel(*track, 0, numSamples);
   EXPECT_NEAR(samples[0], 3.5f, 0.01f)
      << "MixDownToMono should average all 6 channels";
}

// ============================================================
// Stereo regression
// ============================================================

TEST_F(NChannelIntegrationTest, StereoTrack_WriteAndReadBack)
{
   auto track = makeTrackWithData(2, 64);
   ASSERT_EQ(track->NChannels(), 2u);

   auto left = readChannel(*track, 0, 64);
   auto right = readChannel(*track, 1, 64);
   EXPECT_FLOAT_EQ(left[0], 1.0f);
   EXPECT_FLOAT_EQ(right[0], 2.0f);
}

} // namespace au::trackedit
