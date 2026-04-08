/*
 * Audacity: A Digital Audio Editor
 */

/*! Tests for ImportUtils::RequiredTrackCountFromChannels.
 *
 * This pure function determines how many WaveTrack objects to create
 * when importing a file with N channels. Currently it splits >2ch
 * into N mono tracks. After the fix, it should always return 1
 * (one N-channel track).
 *
 * TDD: tests written FIRST to document desired behavior.
 */

#include <gtest/gtest.h>

#include "au3-import-export/ImportUtils.h"

namespace au::trackedit {

// ============================================================
// Regression guards: mono and stereo already return 1
// ============================================================

TEST(ImportNChannel, RequiredTrackCount_Mono_Returns1)
{
   // Already passing - regression guard
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(1), 1);
}

TEST(ImportNChannel, RequiredTrackCount_Stereo_Returns1)
{
   // Already passing - regression guard
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(2), 1);
}

// ============================================================
// N-channel: should return 1 (one wide track), not N mono tracks
// ============================================================

TEST(ImportNChannel, RequiredTrackCount_SixChannels_Returns1)
{
   // BUG: currently returns 6 (splits to 6 mono tracks)
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(6), 1)
      << "6-channel file should import as 1 track, not 6";
}

TEST(ImportNChannel, RequiredTrackCount_EightChannels_Returns1)
{
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(8), 1)
      << "8-channel file should import as 1 track";
}

TEST(ImportNChannel, RequiredTrackCount_SixteenChannels_Returns1)
{
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(16), 1)
      << "16-channel file should import as 1 track";
}

TEST(ImportNChannel, RequiredTrackCount_ThirtyTwoChannels_Returns1)
{
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(32), 1)
      << "32-channel file should import as 1 track";
}

TEST(ImportNChannel, RequiredTrackCount_ThreeChannels_Returns1)
{
   // BUG: currently returns 3 (first value above stereo that gets split)
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(3), 1)
      << "3-channel file should import as 1 track";
}

// ============================================================
// Boundary: degenerate/invalid inputs
// ============================================================

TEST(ImportNChannel, RequiredTrackCount_ZeroChannels_Returns0)
{
   // BUG: currently returns 1 (the <= 2 branch catches 0).
   // A file with 0 channels is malformed; 0 tracks is the safe answer.
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(0), 0)
      << "0 channels should produce 0 tracks";
}

TEST(ImportNChannel, RequiredTrackCount_NegativeChannels_Returns0)
{
   // BUG: currently returns 1 (the <= 2 branch catches negatives).
   // Negative channel count is invalid; treat as 0.
   EXPECT_EQ(ImportUtils::RequiredTrackCountFromChannels(-1), 0)
      << "Negative channels should produce 0 tracks";
}

} // namespace au::trackedit
