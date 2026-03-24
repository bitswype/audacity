/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  DownmixStageTests.cpp

  Tests for DownmixStage construction and basic properties.

**********************************************************************/

#include "au3-mixer/DownmixStage.h"
#include "au3-mixer/DownmixSource.h"
#include "au3-audio-graph/AudioGraphSource.h"
#include "au3-audio-graph/AudioGraphBuffers.h"
#include "au3-math/SampleCount.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

// Minimal source that does nothing -- we're testing DownmixStage
// construction and properties, not audio processing.
class NullAudioSource final : public AudioGraph::Source
{
public:
   bool AcceptsBuffers(const Buffers&) const override { return true; }
   bool AcceptsBlockSize(size_t) const override { return true; }
   std::optional<size_t> Acquire(Buffers&, size_t) override { return 0; }
   sampleCount Remaining() const override { return sampleCount(0); }
   bool Release() override { return true; }
};

} // namespace

TEST(DownmixStage, ConstructsWithStereoChannels)
{
   NullAudioSource audioSrc;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(audioSrc, 2));

   DownmixStage stage(
      std::move(sources), 2, 512, DownmixStage::ApplyVolume::MapChannels);

   EXPECT_TRUE(stage.AcceptsBlockSize(512));
   EXPECT_TRUE(stage.AcceptsBlockSize(256));
   EXPECT_FALSE(stage.AcceptsBlockSize(1024));
}

TEST(DownmixStage, ConstructsWithSixChannels)
{
   NullAudioSource audioSrc;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(audioSrc, 6));

   // 6-channel construction should not crash
   DownmixStage stage(
      std::move(sources), 6, 512, DownmixStage::ApplyVolume::MapChannels);

   EXPECT_TRUE(stage.AcceptsBlockSize(512));
}

TEST(DownmixStage, AcceptsBuffersMatchesChannelCount)
{
   NullAudioSource audioSrc;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(audioSrc, 2));

   DownmixStage stage(
      std::move(sources), 2, 512, DownmixStage::ApplyVolume::MapChannels);

   AudioGraph::Buffers buf2(2, 512, 1);
   AudioGraph::Buffers buf6(6, 512, 1);

   EXPECT_TRUE(stage.AcceptsBuffers(buf2));
   EXPECT_FALSE(stage.AcceptsBuffers(buf6));
}
