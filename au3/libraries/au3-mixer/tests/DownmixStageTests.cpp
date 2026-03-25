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

// ---------- Boundary tests: 3 and 4 channels ----------
// 3 was the old hardcoded value (the boundary that used to work).
// 4 is the first count that the old code would have truncated.

TEST(DownmixStage, ThreeChannelBoundary)
{
   const size_t numChannels = 3;
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
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Channel " << c << " in 3-channel (old boundary)";
   }
}

TEST(DownmixStage, FourChannelFirstNewBoundary)
{
   const size_t numChannels = 4;
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
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Channel " << c << " in 4-channel (first count old code drops)";
   }
}

// ---------- Downmix: source wider than output ----------

TEST(DownmixStage, SixChannelSourceToStereoOutput)
{
   // 6-channel source mixed down to 2 outputs.
   // SimpleDonwmixSource identity routing: channels 0,1 -> outputs 0,1;
   // channels 2-5 have channelFlags[c] = (c == iChannel), but since
   // numChannels is 2, channels 2+ map to nothing (flags are all 0).
   // The point: mFloatBuffers must still be wide enough to READ all 6
   // source channels, even though only 2 outputs exist.
   const size_t srcChannels = 6;
   const size_t outChannels = 2;
   const size_t blockSize = 64;

   ChannelIdentifyingSource src;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(src, srcChannels));

   DownmixStage stage(
      std::move(sources), outChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(outChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // Channels 0 and 1 should have their identifying values
   for (unsigned c = 0; c < outChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Output " << c << " from 6ch->2ch downmix";
   }
}

// ---------- Mixed-width sources in the same stage ----------

TEST(DownmixStage, MixedWidthSources_WideAndNarrow)
{
   // Two sources: a 6-channel and a 2-channel, mixed to 6 outputs.
   // The 6-ch source writes [1..6], the 2-ch source writes [1,2].
   // After mixing, outputs 0 and 1 should have the sum of both sources,
   // outputs 2-5 should have only the 6-ch source's data.
   const size_t outChannels = 6;
   const size_t blockSize = 64;

   ChannelIdentifyingSource wideSrc;
   ChannelIdentifyingSource narrowSrc;
   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(wideSrc, 6));
   sources.push_back(
      std::make_unique<SimpleDonwmixSource>(narrowSrc, 2));

   DownmixStage stage(
      std::move(sources), outChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(outChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // Output 0: wideSrc ch0 (1.0) + narrowSrc ch0 (1.0) = 2.0
   // Output 1: wideSrc ch1 (2.0) + narrowSrc ch1 (2.0) = 4.0
   const float* d0 = reinterpret_cast<const float*>(
      output.GetReadPosition(0));
   const float* d1 = reinterpret_cast<const float*>(
      output.GetReadPosition(1));
   EXPECT_FLOAT_EQ(d0[0], 2.0f) << "Output 0: sum of both sources ch0";
   EXPECT_FLOAT_EQ(d1[0], 4.0f) << "Output 1: sum of both sources ch1";

   // Outputs 2-5: only wideSrc contributes
   for (unsigned c = 2; c < outChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
         << "Output " << c << " should only have wide source data";
   }
}
