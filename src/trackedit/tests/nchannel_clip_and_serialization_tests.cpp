/*
 * Audacity: A Digital Audio Editor
 */

/*! Tests for N-channel WaveClip generalization.
 *
 * Verifies that WaveClip::MakeStereo can merge clips beyond 2 channels,
 * that merged sequences are independent, and that stereo still works.
 *
 * TDD: tests written FIRST to document desired behavior.
 */

#include <gtest/gtest.h>

#include "au3interactiontestbase.h"

#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-math/SampleFormat.h"

#include <set>

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
};

// ============================================================
// WaveClip::MakeStereo: merge beyond 2 channels
// ============================================================

TEST_F(NChannelClipTest, MergeSixMono_ProducesSixChannelClip)
{
   const size_t numSamples = 64;
   auto baseClip = makeMonoClip(numSamples, 1.0f);
   ASSERT_EQ(baseClip->NChannels(), 1u);

   // Merge 5 more mono clips, each with a distinct DC value.
   // BUG: MakeStereo asserts NChannels() == 1 on the receiver.
   // After the first merge, baseClip has 2 channels, so the second
   // merge hits the assertion in debug or silently truncates in release.
   for (int i = 1; i < 6; ++i) {
      auto otherClip = makeMonoClip(numSamples, static_cast<float>(i + 1));
      baseClip->MakeStereo(std::move(*otherClip), true);
   }

   EXPECT_EQ(baseClip->NChannels(), 6u)
      << "After 5 successive merges, clip should have 6 channels";
}

TEST_F(NChannelClipTest, MergeSixMono_SequencesAreIndependent)
{
   // After merging 6 mono clips (each with distinct DC values),
   // all 6 sequences should be distinct objects with distinct data.
   const size_t numSamples = 64;
   auto baseClip = makeMonoClip(numSamples, 1.0f);

   for (int i = 1; i < 6; ++i) {
      auto otherClip = makeMonoClip(numSamples, static_cast<float>(i + 1));
      baseClip->MakeStereo(std::move(*otherClip), true);
   }

   ASSERT_EQ(baseClip->NChannels(), 6u);

   // Verify sequence pointers are all distinct (no aliasing)
   std::set<const void*> seqPtrs;
   for (size_t ch = 0; ch < 6; ++ch) {
      const auto* seq = baseClip->GetSequence(ch);
      ASSERT_NE(seq, nullptr) << "Channel " << ch << " sequence is null";
      seqPtrs.insert(seq);
   }
   EXPECT_EQ(seqPtrs.size(), 6u)
      << "All 6 sequences should be distinct objects";
}

TEST_F(NChannelClipTest, MergeEightMono_ProducesEightChannelClip)
{
   const size_t numSamples = 64;
   auto baseClip = makeMonoClip(numSamples);

   for (int i = 1; i < 8; ++i) {
      auto otherClip = makeMonoClip(numSamples);
      baseClip->MakeStereo(std::move(*otherClip), true);
   }

   EXPECT_EQ(baseClip->NChannels(), 8u);
}

// ============================================================
// Regression: stereo clip merge still works
// ============================================================

TEST_F(NChannelClipTest, MergeTwoMono_StillProducesStereo)
{
   // Already passing - regression guard
   auto left = makeMonoClip(64, 1.0f);
   auto right = makeMonoClip(64, 2.0f);

   left->MakeStereo(std::move(*right), true);

   EXPECT_EQ(left->NChannels(), 2u)
      << "Stereo merge should still produce 2-channel clip";

   // Verify the strong invariant (equal sample counts across channels)
   EXPECT_NO_THROW(left->CheckInvariants());
}

} // namespace au::trackedit
