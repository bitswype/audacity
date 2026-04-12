/*
 * Audacity: A Digital Audio Editor
 */

/*! Tests for N-channel WaveClip and WaveTrack operations.
 *
 * Covers: MakeStereo (merge), DiscardRightChannel, MakeMono (downmix),
 * SwapChannels, SplitChannels, MakeStereo no-arg, and EmptyCopy.
 * Each operation is tested at 2ch (regression) and 6ch (N-channel).
 */

#include <gtest/gtest.h>

#include "au3interactiontestbase.h"

#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-math/SampleFormat.h"

#include <set>
#include <string>

#include "au3-xml/XMLWriter.h"

using namespace au;
using namespace au::au3;
using ::testing::NiceMock;
using ::testing::Return;

namespace au::trackedit {

class NChannelClipTest : public Au3InteractionTestBase
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

   WaveTrackFactory& trackFactory()
   {
      return WaveTrackFactory::Get(projectRef());
   }

   const SampleBlockFactoryPtr& sampleBlockFactory()
   {
      return trackFactory().GetSampleBlockFactory();
   }

   // Helper: create a mono clip with N samples filled with a constant
   std::shared_ptr<WaveClip> makeMonoClip(size_t numSamples, float value = 0.0f)
   {
      auto clip = WaveClip::NewShared(
         1, sampleBlockFactory(), floatSample, 44100);
      std::vector<float> data(numSamples, value);
      constSamplePtr buf = reinterpret_cast<constSamplePtr>(data.data());
      clip->Append(&buf, floatSample, numSamples, 1, floatSample);
      clip->Flush();
      return clip;
   }

   // Helper: merge N mono clips into one N-channel clip
   std::shared_ptr<WaveClip> makeNChannelClip(size_t nChannels, size_t numSamples)
   {
      auto clip = makeMonoClip(numSamples, 1.0f);
      for (size_t i = 1; i < nChannels; ++i) {
         auto other = makeMonoClip(numSamples, static_cast<float>(i + 1));
         clip->MakeStereo(std::move(*other), true);
      }
      return clip;
   }

   // Helper: create an N-channel track with one clip of silence
   std::shared_ptr<WaveTrack> makeNChannelTrack(size_t nChannels, size_t numSamples = 64)
   {
      auto track = trackFactory().Create(nChannels, floatSample, 44100.0);
      std::vector<float> silence(numSamples, 0.0f);
      constSamplePtr buf = reinterpret_cast<constSamplePtr>(silence.data());
      auto clip = track->CreateClip(0.0, wxT("test"));
      // Append to each channel via the clip's multi-channel Append
      std::vector<constSamplePtr> bufs(nChannels);
      std::vector<std::vector<float>> channelData(nChannels);
      for (size_t ch = 0; ch < nChannels; ++ch) {
         channelData[ch].assign(numSamples, static_cast<float>(ch + 1));
         bufs[ch] = reinterpret_cast<constSamplePtr>(channelData[ch].data());
      }
      clip->Append(bufs.data(), floatSample, numSamples, 1, floatSample);
      clip->Flush();
      track->InsertInterval(clip, true);
      return track;
   }
};

// ============================================================
// WaveClip::MakeStereo: merge beyond 2 channels
// ============================================================

TEST_F(NChannelClipTest, MergeSixMono_ProducesSixChannelClip)
{
   auto clip = makeNChannelClip(6, 64);
   EXPECT_EQ(clip->NChannels(), 6u);
}

TEST_F(NChannelClipTest, MergeSixMono_SequencesAreIndependent)
{
   auto clip = makeNChannelClip(6, 64);
   ASSERT_EQ(clip->NChannels(), 6u);

   std::set<const void*> seqPtrs;
   for (size_t ch = 0; ch < 6; ++ch) {
      const auto* seq = clip->GetSequence(ch);
      ASSERT_NE(seq, nullptr) << "Channel " << ch << " sequence is null";
      seqPtrs.insert(seq);
   }
   EXPECT_EQ(seqPtrs.size(), 6u)
      << "All 6 sequences should be distinct objects";
}

TEST_F(NChannelClipTest, MergeEightMono_ProducesEightChannelClip)
{
   auto clip = makeNChannelClip(8, 64);
   EXPECT_EQ(clip->NChannels(), 8u);
}

// ============================================================
// WaveClip::DiscardRightChannel on N>2 clips
// ============================================================

TEST_F(NChannelClipTest, DiscardRightChannel_SixChannelClip_BecomeMono)
{
   auto clip = makeNChannelClip(6, 64);
   ASSERT_EQ(clip->NChannels(), 6u);

   clip->DiscardRightChannel();

   EXPECT_EQ(clip->NChannels(), 1u)
      << "DiscardRightChannel should reduce 6-channel clip to mono";
}

TEST_F(NChannelClipTest, DiscardRightChannel_MonoClip_NoOp)
{
   auto clip = makeMonoClip(64, 1.0f);
   ASSERT_EQ(clip->NChannels(), 1u);

   // Should be a no-op (while loop guard: size > 1)
   clip->DiscardRightChannel();

   EXPECT_EQ(clip->NChannels(), 1u);
}

// ============================================================
// WaveClip::SwapChannels on N>2 clips
// ============================================================

TEST_F(NChannelClipTest, SwapChannels_SixChannelClip_SwapsFirstTwo)
{
   auto clip = makeNChannelClip(6, 64);
   ASSERT_EQ(clip->NChannels(), 6u);

   // Get sequence pointers before swap
   const auto* seq0Before = clip->GetSequence(0);
   const auto* seq1Before = clip->GetSequence(1);
   const auto* seq2Before = clip->GetSequence(2);

   clip->SwapChannels();

   // Channels 0 and 1 should be swapped
   EXPECT_EQ(clip->GetSequence(0), seq1Before) << "Ch 0 should now have old ch 1";
   EXPECT_EQ(clip->GetSequence(1), seq0Before) << "Ch 1 should now have old ch 0";
   // Channels 2+ should be untouched
   EXPECT_EQ(clip->GetSequence(2), seq2Before) << "Ch 2 should be unchanged";
   EXPECT_EQ(clip->NChannels(), 6u) << "Channel count should not change";
}

// ============================================================
// WaveClip::SplitChannels on N>2 clips
// ============================================================

TEST_F(NChannelClipTest, SplitChannels_SixChannelClip_SplitsLastChannel)
{
   auto clip = makeNChannelClip(6, 64);
   ASSERT_EQ(clip->NChannels(), 6u);

   auto splitOff = clip->SplitChannels();

   EXPECT_EQ(clip->NChannels(), 5u)
      << "Original clip should have 5 channels after split";
   ASSERT_NE(splitOff, nullptr);
   EXPECT_EQ(splitOff->NChannels(), 1u)
      << "Split-off clip should be mono";
}

TEST_F(NChannelClipTest, SplitChannels_RepeatedSplits_ReduceToMono)
{
   auto clip = makeNChannelClip(4, 64);
   ASSERT_EQ(clip->NChannels(), 4u);

   // Split 3 times to reduce from 4 -> 3 -> 2 -> 1
   for (int i = 0; i < 3; ++i) {
      auto splitOff = clip->SplitChannels();
      ASSERT_NE(splitOff, nullptr);
      EXPECT_EQ(splitOff->NChannels(), 1u);
   }

   EXPECT_EQ(clip->NChannels(), 1u)
      << "After 3 splits, 4-channel clip should be mono";
}

// ============================================================
// WaveClip::MakeStereo no-arg on N>2 clips
// ============================================================

TEST_F(NChannelClipTest, MakeStereoNoArg_FourChannelClip_NoOp)
{
   auto clip = makeNChannelClip(4, 64);
   ASSERT_EQ(clip->NChannels(), 4u);

   // MakeStereo() no-arg returns early for NChannels() >= 2
   clip->MakeStereo();

   EXPECT_EQ(clip->NChannels(), 4u)
      << "No-arg MakeStereo should be no-op for 4-channel clip";
}

TEST_F(NChannelClipTest, MakeStereoNoArg_MonoClip_WidensToStereo)
{
   auto clip = makeMonoClip(64, 1.0f);
   ASSERT_EQ(clip->NChannels(), 1u);

   clip->MakeStereo();

   EXPECT_EQ(clip->NChannels(), 2u)
      << "No-arg MakeStereo should widen mono to stereo";
}

// ============================================================
// WaveClip::MakeMono (downmix) on N>2 clips
// ============================================================

TEST_F(NChannelClipTest, MakeMono_SixChannelClip_ReducesToMono)
{
   auto clip = makeNChannelClip(6, 64);
   ASSERT_EQ(clip->NChannels(), 6u);

   auto progress = [](double) {};
   auto cancel = []() { return false; };
   bool ok = clip->MakeMono(progress, cancel);

   EXPECT_TRUE(ok);
   EXPECT_EQ(clip->NChannels(), 1u)
      << "MakeMono should reduce 6-channel clip to mono";
}

// ============================================================
// WaveTrack-level operations
// ============================================================

TEST_F(NChannelClipTest, Track_MakeMono_SixChannelTrack)
{
   auto track = trackFactory().Create(6, floatSample, 44100.0);
   ASSERT_EQ(track->NChannels(), 6u);

   track->MakeMono();

   EXPECT_EQ(track->NChannels(), 1u)
      << "MakeMono should reduce 6-channel track to mono";
}

TEST_F(NChannelClipTest, Track_SwapChannels_SixChannelTrack)
{
   auto track = trackFactory().Create(6, floatSample, 44100.0);
   ASSERT_EQ(track->NChannels(), 6u);

   // Should not crash - swaps channels 0 and 1
   track->SwapChannels();

   EXPECT_EQ(track->NChannels(), 6u)
      << "SwapChannels should preserve channel count";
}

TEST_F(NChannelClipTest, Track_EmptyCopy_SixChannels)
{
   auto track = trackFactory().Create(6, floatSample, 44100.0);
   auto copy = track->EmptyCopy(6);
   ASSERT_NE(copy, nullptr);
   EXPECT_EQ(copy->NChannels(), 6u);
}

// ============================================================
// Regression: stereo operations still work
// ============================================================

TEST_F(NChannelClipTest, MergeTwoMono_StillProducesStereo)
{
   auto left = makeMonoClip(64, 1.0f);
   auto right = makeMonoClip(64, 2.0f);
   left->MakeStereo(std::move(*right), true);
   EXPECT_EQ(left->NChannels(), 2u);
   EXPECT_NO_THROW(left->CheckInvariants());
}

TEST_F(NChannelClipTest, StereoClip_SwapChannels_StillWorks)
{
   auto clip = makeNChannelClip(2, 64);
   const auto* seq0 = clip->GetSequence(0);
   const auto* seq1 = clip->GetSequence(1);
   clip->SwapChannels();
   EXPECT_EQ(clip->GetSequence(0), seq1);
   EXPECT_EQ(clip->GetSequence(1), seq0);
}

TEST_F(NChannelClipTest, StereoClip_SplitChannels_StillWorks)
{
   auto clip = makeNChannelClip(2, 64);
   auto splitOff = clip->SplitChannels();
   EXPECT_EQ(clip->NChannels(), 1u);
   ASSERT_NE(splitOff, nullptr);
   EXPECT_EQ(splitOff->NChannels(), 1u);
}

// ============================================================
// WidenToChannels: mono clip widened to match N-channel track
// ============================================================

TEST_F(NChannelClipTest, WidenToChannels_MonoToSix)
{
   auto clip = makeMonoClip(64, 1.0f);
   ASSERT_EQ(clip->NChannels(), 1u);

   clip->WidenToChannels(6);

   EXPECT_EQ(clip->NChannels(), 6u)
      << "WidenToChannels(6) should produce a 6-channel clip";
}

TEST_F(NChannelClipTest, WidenToChannels_AlreadyWideEnough)
{
   auto clip = makeNChannelClip(4, 64);
   ASSERT_EQ(clip->NChannels(), 4u);

   clip->WidenToChannels(4);

   EXPECT_EQ(clip->NChannels(), 4u)
      << "WidenToChannels should be a no-op when clip already has enough channels";
}

TEST_F(NChannelClipTest, WidenToChannels_StereoToSix)
{
   auto clip = makeNChannelClip(2, 64);
   ASSERT_EQ(clip->NChannels(), 2u);

   clip->WidenToChannels(6);

   EXPECT_EQ(clip->NChannels(), 6u)
      << "WidenToChannels(6) should widen a stereo clip to 6 channels";
}

// ============================================================
// Serialization: nchannels attribute
// ============================================================

TEST_F(NChannelClipTest, SixChannelTrack_WriteXML_ContainsNChannelsAttribute)
{
   auto track = makeNChannelTrack(6, 64);
   ASSERT_EQ(track->NChannels(), 6u);

   XMLStringWriter xml;
   track->WriteXML(xml);
   std::string output(xml.mb_str());

   EXPECT_NE(output.find("nchannels=\"6\""), std::string::npos)
      << "WriteXML should include nchannels attribute for 6-channel track.\n"
      << "XML output: " << output.substr(0, 200);
}

TEST_F(NChannelClipTest, StereoTrack_WriteXML_NoNChannelsAttribute)
{
   auto track = makeNChannelTrack(2, 64);
   ASSERT_EQ(track->NChannels(), 2u);

   XMLStringWriter xml;
   track->WriteXML(xml);
   std::string output(xml.mb_str());

   EXPECT_EQ(output.find("nchannels="), std::string::npos)
      << "WriteXML should NOT include nchannels for stereo (<=2ch) tracks.\n"
      << "XML output: " << output.substr(0, 200);
}

TEST_F(NChannelClipTest, MonoTrack_WriteXML_NoNChannelsAttribute)
{
   auto track = trackFactory().Create(1, floatSample, 44100.0);

   XMLStringWriter xml;
   track->WriteXML(xml);
   std::string output(xml.mb_str());

   EXPECT_EQ(output.find("nchannels="), std::string::npos)
      << "WriteXML should NOT include nchannels for mono tracks.";
}

} // namespace au::trackedit
