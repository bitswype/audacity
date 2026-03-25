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

// Source that writes channel-identifying data into buffers.
// Channel c gets filled with float(c + 1) for every sample, so we can
// verify that each channel's data survives the DownmixStage pipeline.
class ChannelIdentifyingSource final : public AudioGraph::Source
{
public:
   bool AcceptsBuffers(const Buffers&) const override { return true; }
   bool AcceptsBlockSize(size_t) const override { return true; }

   std::optional<size_t> Acquire(Buffers& buffers, size_t maxToProcess) override
   {
      for (unsigned c = 0; c < buffers.Channels(); ++c) {
         float* p = &buffers.GetWritePosition(c);
         for (size_t i = 0; i < maxToProcess; ++i)
            p[i] = static_cast<float>(c + 1);
      }
      return maxToProcess;
   }

   sampleCount Remaining() const override { return sampleCount(10000); }
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

// ---------- Bug: mFloatBuffers{3} drops channels beyond 2 ----------
//
// DownmixStage allocates mFloatBuffers with exactly 3 channels (hardcoded),
// then clamps processing to max(2, mFloatBuffers.Channels()) = 3.
// Any source with >3 channels silently loses channels 3+.

TEST(DownmixStage, SixChannelAcquirePreservesAllChannels)
{
   const size_t numChannels = 6;
   const size_t blockSize = 64;

   ChannelIdentifyingSource src;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(src, numChannels));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // Each output channel c should contain float(c+1) from our source.
   // With the mFloatBuffers{3} bug, channels 3-5 will be zero.
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Channel " << c << " should have value " << (c + 1)
         << " but the mFloatBuffers{3} bug drops channels beyond 2";
   }
}

TEST(DownmixStage, EightChannelAcquirePreservesAllChannels)
{
   const size_t numChannels = 8;
   const size_t blockSize = 64;

   ChannelIdentifyingSource src;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(src, numChannels));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Channel " << c << " lost in 8-channel pipeline";
   }
}

TEST(DownmixStage, SixteenChannelAcquirePreservesAllChannels)
{
   const size_t numChannels = 16;
   const size_t blockSize = 64;

   ChannelIdentifyingSource src;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(src, numChannels));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Channel " << c << " lost in 16-channel pipeline";
   }
}

TEST(DownmixStage, StereoStillWorksAfterFix)
{
   // Regression: make sure the fix doesn't break stereo behavior
   const size_t numChannels = 2;
   const size_t blockSize = 64;

   ChannelIdentifyingSource src;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(src, numChannels));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Stereo channel " << c << " broken by multi-channel fix";
   }
}
