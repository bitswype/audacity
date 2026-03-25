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
#include "FakePlayableSequence.h"

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

// Source that writes per-sample ramp data: channel c, sample i = c*1000+i.
// This lets us verify every sample position, not just sample 0.
class RampSource final : public AudioGraph::Source
{
public:
   bool AcceptsBuffers(const Buffers&) const override { return true; }
   bool AcceptsBlockSize(size_t) const override { return true; }

   std::optional<size_t> Acquire(Buffers& buffers, size_t maxToProcess) override
   {
      for (unsigned c = 0; c < buffers.Channels(); ++c) {
         float* p = &buffers.GetWritePosition(c);
         for (size_t i = 0; i < maxToProcess; ++i)
            p[i] = static_cast<float>(c * 1000 + i);
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

// ======================================================================
// Multi-sample verification: check every sample, not just data[0]
// ======================================================================

TEST(DownmixStage, SixChannel_AllSamplesPreserved)
{
   const size_t numChannels = 6;
   const size_t blockSize = 32;

   RampSource src;
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

   // RampSource: channel c, sample i = c*1000 + i
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      for (size_t i = 0; i < blockSize; ++i) {
         EXPECT_FLOAT_EQ(data[i], static_cast<float>(c * 1000 + i))
            << "Channel " << c << " sample " << i;
      }
   }
}

// ======================================================================
// Multi-call Acquire: verify Advance/Rotate state across calls
// ======================================================================

TEST(DownmixStage, MultipleAcquireCalls_StatePreserved)
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

   // Call Acquire three times
   for (int call = 0; call < 3; ++call) {
      AudioGraph::Buffers output(numChannels, blockSize, 1);
      auto result = stage.Acquire(output, blockSize);

      ASSERT_TRUE(result.has_value()) << "Call " << call;
      EXPECT_EQ(*result, blockSize) << "Call " << call;

      for (unsigned c = 0; c < numChannels; ++c) {
         const float* data = reinterpret_cast<const float*>(
            output.GetReadPosition(c));
         EXPECT_FLOAT_EQ(data[0], static_cast<float>(c + 1))
            << "Call " << call << " channel " << c;
      }
   }
}

// ======================================================================
// MapChannels mode: SequenceDownmixSource with per-channel gain
// ======================================================================

TEST(DownmixStage, MapChannels_AppliesPerChannelGain)
{
   // 4-channel source, each channel has DC=1.0.
   // Per-channel gains: [0.5, 0.8, 0.3, 1.0]
   // Expected output: channel c = 1.0 * gain[c]
   const size_t numChannels = 4;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc;
   auto seq = FakePlayableSequence::DC(
      numChannels, 44100, 1024, {1.0f, 1.0f, 1.0f, 1.0f});
   seq.SetChannelVolumes({0.5f, 0.8f, 0.3f, 1.0f});

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::MapChannels);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // ChannelIdentifyingSource writes float(c+1) per channel.
   // MapChannels applies volumes[c] = GetChannelGain(c) for all source
   // channels (when numChannels > 1). With identity routing, source
   // channel c -> output c, so output[c] = float(c+1) * gain[c].
   const float gains[] = {0.5f, 0.8f, 0.3f, 1.0f};
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      const float expected = static_cast<float>(c + 1) * gains[c];
      EXPECT_NEAR(data[0], expected, 1e-5f)
         << "Channel " << c << " with gain " << gains[c];
   }
}

TEST(DownmixStage, MapChannels_SixChannelWithGain)
{
   const size_t numChannels = 6;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc;
   auto seq = FakePlayableSequence::DC(
      numChannels, 44100, 1024,
      {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
   seq.SetChannelVolumes({0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

   DownmixStage stage(
      std::move(sources), numChannels, blockSize,
      DownmixStage::ApplyVolume::MapChannels);

   AudioGraph::Buffers output(numChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());

   const float gains[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
   for (unsigned c = 0; c < numChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      const float expected = static_cast<float>(c + 1) * gains[c];
      EXPECT_NEAR(data[0], expected, 1e-5f)
         << "Channel " << c << " with gain " << gains[c];
   }
}

// ======================================================================
// Mixdown mode: stereo to mono with averaging
// ======================================================================

TEST(DownmixStage, Mixdown_StereoToMono_Averages)
{
   // Stereo source -> mono output with Mixdown.
   // ChannelIdentifyingSource: ch0=1.0, ch1=2.0
   // Mixdown divides by limit (2), CanMakeMono() is true.
   // With gain 1.0, output = (1.0/2 + 2.0/2) = 1.5
   const size_t outChannels = 1;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc;
   auto seq = FakePlayableSequence::DC(2, 44100, 1024, {1.0f, 1.0f});
   // Default volume 1.0, default pan 0.0 -> both channels gain 1.0

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

   DownmixStage stage(
      std::move(sources), outChannels, blockSize,
      DownmixStage::ApplyVolume::Mixdown);

   AudioGraph::Buffers output(outChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // Both source channels route to output 0 (mono fan-out for IsMono=false,
   // single output). ch0=1.0*1.0/2 + ch1=2.0*1.0/2 = 0.5 + 1.0 = 1.5
   const float* data = reinterpret_cast<const float*>(
      output.GetReadPosition(0));
   EXPECT_NEAR(data[0], 1.5f, 1e-5f)
      << "Mixdown stereo->mono should average";
}

// ======================================================================
// SequenceDownmixSource through full Acquire pipeline
// ======================================================================

TEST(DownmixStage, SequenceDownmixSource_MonoFanout)
{
   // Mono source through SequenceDownmixSource to 4-channel output.
   // Mono fan-out: all output channels get the source data.
   const size_t outChannels = 4;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc; // ch0 = 1.0
   auto seq = FakePlayableSequence::DC(1, 44100, 1024, {1.0f});

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

   DownmixStage stage(
      std::move(sources), outChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(outChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   // Mono fan-out: all outputs get ch0 data (1.0)
   for (unsigned c = 0; c < outChannels; ++c) {
      const float* data = reinterpret_cast<const float*>(
         output.GetReadPosition(c));
      EXPECT_FLOAT_EQ(data[0], 1.0f)
         << "Mono fan-out: output " << c << " should get source data";
   }
}

TEST(DownmixStage, SequenceDownmixSource_SixChannelIdentity)
{
   // 6-channel source through SequenceDownmixSource to 6-channel output.
   // Identity routing: channel N -> output N.
   const size_t numChannels = 6;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc;
   auto seq = FakePlayableSequence::DC(
      numChannels, 44100, 1024,
      {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

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
         << "Identity routing: output " << c;
   }
}

TEST(DownmixStage, SequenceDownmixSource_ChannelBeyondRange_RoutesToOutput0)
{
   // 4-channel source routed to 2-channel output.
   // SequenceDownmixSource routes: ch0->out0, ch1->out1,
   // ch2->out0 (fallback), ch3->out0 (fallback).
   // ChannelIdentifyingSource: ch0=1, ch1=2, ch2=3, ch3=4
   // Output 0 = ch0(1) + ch2(3) + ch3(4) = 8
   // Output 1 = ch1(2)
   const size_t srcChannels = 4;
   const size_t outChannels = 2;
   const size_t blockSize = 32;

   ChannelIdentifyingSource audioSrc;
   auto seq = FakePlayableSequence::DC(
      srcChannels, 44100, 1024, {1.0f, 1.0f, 1.0f, 1.0f});

   std::vector<std::unique_ptr<DownmixSource>> sources;
   sources.push_back(
      std::make_unique<SequenceDownmixSource>(audioSrc, seq, nullptr));

   DownmixStage stage(
      std::move(sources), outChannels, blockSize,
      DownmixStage::ApplyVolume::Discard);

   AudioGraph::Buffers output(outChannels, blockSize, 1);
   auto result = stage.Acquire(output, blockSize);

   ASSERT_TRUE(result.has_value());
   EXPECT_EQ(*result, blockSize);

   const float* d0 = reinterpret_cast<const float*>(
      output.GetReadPosition(0));
   const float* d1 = reinterpret_cast<const float*>(
      output.GetReadPosition(1));
   // ch0(1.0) + ch2(3.0) + ch3(4.0) = 8.0 on output 0
   EXPECT_FLOAT_EQ(d0[0], 8.0f)
      << "Output 0 gets ch0 + overflow channels 2,3";
   // ch1(2.0) on output 1
   EXPECT_FLOAT_EQ(d1[0], 2.0f)
      << "Output 1 gets ch1 only";
}
