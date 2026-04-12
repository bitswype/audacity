/*
 * Audacity: A Digital Audio Editor
 */

/*! Tests for N-channel WaveTrack support.
 *
 * These tests verify that WaveTrack can be created with >2 channels,
 * that the factory produces a single wide track (not N mono tracks),
 * that channel access works for any index, and that channels are
 * independent (distinct objects, not aliases of the same channel).
 *
 * TDD: tests are written FIRST to document desired behavior, then the
 * production code is changed to make them pass.
 */

#include <gtest/gtest.h>

#include "au3interactiontestbase.h"

#include "au3-wave-track/WaveTrack.h"
#include "au3-math/SampleFormat.h"

#include <set>

using namespace au;
using namespace au::au3;
using ::testing::NiceMock;
using ::testing::Return;

namespace au::trackedit {

class WaveTrackNChannelTest : public Au3InteractionTestBase
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

   // Helper: get the first WaveTrack from a TrackListHolder, or null.
   const WaveTrack* firstWaveTrack(const TrackListHolder& holder)
   {
      auto range = holder->Any<WaveTrack>();
      auto it = range.begin();
      if (it == range.end())
         return nullptr;
      return *it;
   }

   // Helper: count all tracks in a TrackListHolder.
   size_t countTracks(const TrackListHolder& holder)
   {
      size_t n = 0;
      for ([[maybe_unused]] auto t : holder->Any())
         ++n;
      return n;
   }
};

// ============================================================
// CreateMany: should produce ONE track, not N mono tracks
// ============================================================

TEST_F(WaveTrackNChannelTest, CreateMany_SixChannels_ProducesSingleTrack)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(6, floatSample, 44100.0);

   // BUG: current code creates 6 separate mono tracks.
   // DESIRED: 1 track with 6 channels.
   EXPECT_EQ(countTracks(holder), 1u)
      << "CreateMany(6) should produce 1 six-channel track";
}

TEST_F(WaveTrackNChannelTest, CreateMany_SixChannels_TrackHasSixChannels)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(6, floatSample, 44100.0);

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr) << "Should have at least one WaveTrack";

   EXPECT_EQ(track->NChannels(), 6u)
      << "The track should report 6 channels";
}

TEST_F(WaveTrackNChannelTest, CreateMany_EightChannels_ProducesSingleTrack)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(8, floatSample, 44100.0);

   EXPECT_EQ(countTracks(holder), 1u)
      << "CreateMany(8) should produce 1 eight-channel track";
}

TEST_F(WaveTrackNChannelTest, CreateMany_SixteenChannels_TrackHasSixteenChannels)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(16, floatSample, 44100.0);

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr);

   EXPECT_EQ(track->NChannels(), 16u)
      << "The track should report 16 channels";
}

// ============================================================
// Stereo and mono regression tests
// ============================================================

TEST_F(WaveTrackNChannelTest, CreateMany_Stereo_StillProducesSingleTrack)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(2, floatSample, 44100.0);

   EXPECT_EQ(countTracks(holder), 1u)
      << "CreateMany(2) should still produce 1 stereo track";

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr);
   EXPECT_EQ(track->NChannels(), 2u);
}

TEST_F(WaveTrackNChannelTest, CreateMany_Mono_StillProducesSingleTrack)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(1, floatSample, 44100.0);

   EXPECT_EQ(countTracks(holder), 1u);

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr);
   EXPECT_EQ(track->NChannels(), 1u);
}

// ============================================================
// Channel access: GetChannel(i) returns non-null for all i
// ============================================================

TEST_F(WaveTrackNChannelTest, SixChannelTrack_GetChannel_AllIndicesValid)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(6, floatSample, 44100.0);

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr);
   ASSERT_EQ(track->NChannels(), 6u);

   // Every channel index from 0 to 5 should return a non-null channel
   for (size_t i = 0; i < 6; ++i) {
      auto channel = track->GetChannel(i);
      // Compare the shared_ptr value, not its stack address
      EXPECT_NE(channel.get(), nullptr)
         << "GetChannel(" << i << ") should return a non-null channel";
   }
}

// ============================================================
// Channel independence: each channel is a distinct object
// ============================================================

TEST_F(WaveTrackNChannelTest, SixChannelTrack_ChannelsAreDistinct)
{
   auto& factory = WaveTrackFactory::Get(projectRef());
   auto holder = factory.CreateMany(6, floatSample, 44100.0);

   const WaveTrack* track = firstWaveTrack(holder);
   ASSERT_NE(track, nullptr);
   ASSERT_EQ(track->NChannels(), 6u);

   // Collect raw pointers from each GetChannel call.
   // All 6 must be distinct (no aliasing).
   std::set<const void*> channelPtrs;
   for (size_t i = 0; i < 6; ++i) {
      auto channel = track->GetChannel(i);
      ASSERT_NE(channel.get(), nullptr)
         << "GetChannel(" << i << ") returned null";
      channelPtrs.insert(channel.get());
   }

   EXPECT_EQ(channelPtrs.size(), 6u)
      << "All 6 channels should be distinct objects, not aliases";
}

} // namespace au::trackedit
