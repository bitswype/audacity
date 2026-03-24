/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  DownmixSourceTests.cpp

  Tests for channel routing logic in DownmixSource and
  SimpleDonwmixSource. Tests are organized by:
  - Baseline: stereo behavior that must never regress
  - Multi-channel: expected behavior for >2 channels
  - Delegation: verify pass-through to underlying sequence
  - Custom map: the mpMap code path
  - Edge cases: boundary conditions

**********************************************************************/

#include "au3-mixer/DownmixSource.h"
#include "FakePlayableSequence.h"
#include "au3-audio-graph/AudioGraphSource.h"
#include "au3-audio-graph/AudioGraphChannel.h"

#include <gtest/gtest.h>
#include <vector>
#include <numeric>

namespace {

// Stub AudioGraph::Source that asserts if any audio method is called.
// FindChannelFlags and GetChannelGain should never touch the downstream.
class StubAudioSource final : public AudioGraph::Source
{
public:
   bool AcceptsBuffers(const Buffers&) const override
   {
      ADD_FAILURE() << "Unexpected call to StubAudioSource::AcceptsBuffers";
      return false;
   }
   bool AcceptsBlockSize(size_t) const override
   {
      ADD_FAILURE() << "Unexpected call to StubAudioSource::AcceptsBlockSize";
      return false;
   }
   std::optional<size_t> Acquire(Buffers&, size_t) override
   {
      ADD_FAILURE() << "Unexpected call to StubAudioSource::Acquire";
      return 0;
   }
   sampleCount Remaining() const override
   {
      ADD_FAILURE() << "Unexpected call to StubAudioSource::Remaining";
      return 0;
   }
   bool Release() override
   {
      ADD_FAILURE() << "Unexpected call to StubAudioSource::Release";
      return false;
   }
};

// Helper to call FindChannelFlags and return as a vector
std::vector<unsigned char> GetFlags(
   DownmixSource& source, size_t numOutputChannels, size_t iChannel)
{
   std::vector<unsigned char> flags(numOutputChannels, 0);
   source.FindChannelFlags(flags.data(), numOutputChannels, iChannel);
   return flags;
}

size_t CountActiveFlags(const std::vector<unsigned char>& flags)
{
   return std::count_if(flags.begin(), flags.end(),
      [](unsigned char f) { return f != 0; });
}

} // namespace

// ============================================================
// PRECONDITION: Verify FakePlayableSequence triggers correct
// IsMono() behavior -- this is the foundation all routing tests
// depend on.
// ============================================================

TEST(FakePlayableSequence, MonoSequenceIsDetectedAsMono)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 64, {0.5f});
   // IsMono checks GetChannelType() == MonoChannel, NOT NChannels()
   EXPECT_EQ(seq.GetChannelType(), AudioGraph::MonoChannel);
   EXPECT_TRUE(AudioGraph::IsMono(seq));
}

TEST(FakePlayableSequence, StereoSequenceIsNotMono)
{
   auto seq = FakePlayableSequence::DC(2, 48000, 64, {0.5f, 0.5f});
   EXPECT_NE(seq.GetChannelType(), AudioGraph::MonoChannel);
   EXPECT_FALSE(AudioGraph::IsMono(seq));
}

TEST(FakePlayableSequence, SixChannelSequenceIsNotMono)
{
   auto seq = FakePlayableSequence::DC(6, 48000, 64,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   EXPECT_FALSE(AudioGraph::IsMono(seq));
}

// ============================================================
// BASELINE: Stereo routing (must pass before and after changes)
// Note: these test SequenceDownmixSource with iChannel values
// that are handled by the existing stereo branches (0 and 1).
// ============================================================

TEST(SequenceDownmixSource, Baseline_MonoFansOutToAllStereoOutputs)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 1024, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 2, 0);

   EXPECT_EQ(flags[0], 1);
   EXPECT_EQ(flags[1], 1);
}

TEST(SequenceDownmixSource, Baseline_NonMonoChannel0RoutesToOutput0)
{
   // This tests the iChannel==0 branch, NOT channel-type routing.
   // Passes on any non-mono sequence regardless of GetChannelType().
   auto seq = FakePlayableSequence::DC(2, 48000, 1024, {1.0f, 0.0f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 2, 0);

   EXPECT_EQ(flags[0], 1);
   EXPECT_EQ(flags[1], 0);
   EXPECT_EQ(CountActiveFlags(flags), 1u);
}

TEST(SequenceDownmixSource, Baseline_NonMonoChannel1RoutesToOutput1)
{
   auto seq = FakePlayableSequence::DC(2, 48000, 1024, {0.0f, 1.0f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 2, 1);

   EXPECT_EQ(flags[0], 0);
   EXPECT_EQ(flags[1], 1);
}

TEST(SequenceDownmixSource, Baseline_Channel1ToMonoOutputRoutesToOutput0)
{
   auto seq = FakePlayableSequence::DC(2, 48000, 1024, {0.0f, 1.0f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 1, 1);

   EXPECT_EQ(flags[0], 1);
}

TEST(SequenceDownmixSource, Baseline_MonoToMonoRoutesToOutput0)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 1024, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 1, 0);

   EXPECT_EQ(flags[0], 1);
}

// ============================================================
// MULTI-CHANNEL: SequenceDownmixSource behavior for >2 channels
// ============================================================

TEST(SequenceDownmixSource, MonoFansOutToAll6Outputs)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 1024, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 6, 0);

   EXPECT_EQ(CountActiveFlags(flags), 6u);
   for (size_t i = 0; i < 6; ++i)
      EXPECT_EQ(flags[i], 1) << "Output " << i;
}

TEST(SequenceDownmixSource, MonoFansOutToAll16Outputs)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 1024, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 16, 0);

   EXPECT_EQ(CountActiveFlags(flags), 16u);
}

// Channels 0 and 1 use the existing stereo branches.
// These pass on unmodified code -- they are NOT proof of
// multi-channel support, just that the stereo fallback works
// when output count is >2.
TEST(SequenceDownmixSource, SixCh_Channel0UsesStereoFallback)
{
   auto seq = FakePlayableSequence::DC(6, 48000, 1024,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 6, 0);

   // This passes via the existing iChannel==0 stereo branch
   EXPECT_EQ(flags[0], 1);
   EXPECT_EQ(CountActiveFlags(flags), 1u);
}

TEST(SequenceDownmixSource, SixCh_Channel2RoutesToOutput2)
{
   auto seq = FakePlayableSequence::DC(6, 48000, 1024,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 6, 2);
   EXPECT_EQ(flags[2], 1);
   EXPECT_EQ(CountActiveFlags(flags), 1u);
}

TEST(SequenceDownmixSource, SixCh_Channel5RoutesToOutput5)
{
   auto seq = FakePlayableSequence::DC(6, 48000, 1024,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   auto flags = GetFlags(dmx, 6, 5);
   EXPECT_EQ(flags[5], 1);
   EXPECT_EQ(CountActiveFlags(flags), 1u);
}

TEST(SequenceDownmixSource, SixteenCh_IdentityRouting)
{
   auto seq = FakePlayableSequence::DC(16, 48000, 64,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
       0.9f, 1.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   for (size_t ch = 0; ch < 16; ++ch) {
      auto flags = GetFlags(dmx, 16, ch);
      EXPECT_EQ(flags[ch], 1) << "Channel " << ch;
      EXPECT_EQ(CountActiveFlags(flags), 1u) << "Channel " << ch;
   }
}

// ============================================================
// SimpleDonwmixSource: already handles N channels correctly
// ============================================================

TEST(SimpleDonwmixSource, MonoFansOutToAll6Outputs)
{
   StubAudioSource source;
   SimpleDonwmixSource dmx(source, 1);

   auto flags = GetFlags(dmx, 6, 0);
   EXPECT_EQ(CountActiveFlags(flags), 6u);
}

TEST(SimpleDonwmixSource, SixChannelIdentityRouting)
{
   StubAudioSource source;
   SimpleDonwmixSource dmx(source, 6);

   for (size_t ch = 0; ch < 6; ++ch) {
      auto flags = GetFlags(dmx, 6, ch);
      EXPECT_EQ(CountActiveFlags(flags), 1u) << "Channel " << ch;
      EXPECT_EQ(flags[ch], 1) << "Channel " << ch;
   }
}

TEST(SimpleDonwmixSource, SixteenChannelIdentityRouting)
{
   StubAudioSource source;
   SimpleDonwmixSource dmx(source, 16);

   for (size_t ch = 0; ch < 16; ++ch) {
      auto flags = GetFlags(dmx, 16, ch);
      EXPECT_EQ(CountActiveFlags(flags), 1u) << "Channel " << ch;
      EXPECT_EQ(flags[ch], 1) << "Channel " << ch;
   }
}

TEST(SimpleDonwmixSource, ChannelBeyondOutputRange_ProducesZeroFlags)
{
   StubAudioSource source;
   SimpleDonwmixSource dmx(source, 6);

   // Channel 2 with only 2 outputs: identity mapping -> no match
   auto flags = GetFlags(dmx, 2, 2);
   EXPECT_EQ(CountActiveFlags(flags), 0u);
}

// ============================================================
// DELEGATION: Verify SequenceDownmixSource delegates correctly
// ============================================================

TEST(SequenceDownmixSource, NChannelsDelegatesToSequence)
{
   auto seq1 = FakePlayableSequence::DC(1, 48000, 64, {0.5f});
   auto seq6 = FakePlayableSequence::DC(6, 48000, 64,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;

   SequenceDownmixSource dmx1(source, seq1, nullptr);
   SequenceDownmixSource dmx6(source, seq6, nullptr);

   EXPECT_EQ(dmx1.NChannels(), 1u);
   EXPECT_EQ(dmx6.NChannels(), 6u);
}

TEST(SequenceDownmixSource, CanMakeMono_TrueWithoutMap)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 64, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   EXPECT_TRUE(dmx.CanMakeMono());
}

TEST(SequenceDownmixSource, GetDownstreamReturnsConstructorArg)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 64, {0.5f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   EXPECT_EQ(&dmx.GetDownstream(), &source);
}

// ============================================================
// CHANNEL GAIN: Test delegation (not the fake's pan model)
// ============================================================

TEST(SequenceDownmixSource, GainDelegatesToSequenceGetChannelVolume)
{
   auto seq = FakePlayableSequence::DC(1, 48000, 1024, {0.5f});
   // Set explicit per-channel volumes to avoid testing the pan model
   seq.SetChannelVolumes({0.75f, 0.25f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   EXPECT_NEAR(dmx.GetChannelGain(0), 0.75f, 1e-6f);
   EXPECT_NEAR(dmx.GetChannelGain(1), 0.25f, 1e-6f);
}

TEST(SequenceDownmixSource, GainForMultiChannelSequence)
{
   auto seq = FakePlayableSequence::DC(6, 48000, 64,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   seq.SetChannelVolumes({0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
   StubAudioSource source;
   SequenceDownmixSource dmx(source, seq, nullptr);

   for (size_t ch = 0; ch < 6; ++ch) {
      float expected = (ch + 1) * 0.1f;
      EXPECT_NEAR(dmx.GetChannelGain(ch), expected, 1e-6f)
         << "Channel " << ch;
   }
}

TEST(SimpleDonwmixSource, GainIsAlways1)
{
   StubAudioSource source;
   SimpleDonwmixSource dmx(source, 6);

   for (size_t ch = 0; ch < 6; ++ch)
      EXPECT_EQ(dmx.GetChannelGain(ch), 1.0f);
}

// ============================================================
// CUSTOM CHANNEL MAP: mpMap path
// ============================================================

TEST(SequenceDownmixSource, CustomMap_OverridesDefaultRouting)
{
   auto seq = FakePlayableSequence::DC(2, 48000, 64, {1.0f, 0.0f});
   StubAudioSource source;

   // Custom map: channel 0 -> outputs 0 AND 1 (fan out)
   ArrayOf<bool> map0(2u);
   map0[0] = true;
   map0[1] = true;

   // Custom map: channel 1 -> output 1 only
   ArrayOf<bool> map1(2u);
   map1[0] = false;
   map1[1] = true;

   const ArrayOf<bool> maps[] = { std::move(map0), std::move(map1) };

   SequenceDownmixSource dmx(source, seq, maps);

   auto flags0 = GetFlags(dmx, 2, 0);
   EXPECT_EQ(flags0[0], 1);
   EXPECT_EQ(flags0[1], 1);

   auto flags1 = GetFlags(dmx, 2, 1);
   EXPECT_EQ(flags1[0], 0);
   EXPECT_EQ(flags1[1], 1);
}

TEST(SequenceDownmixSource, CanMakeMono_FalseWithMap)
{
   auto seq = FakePlayableSequence::DC(2, 48000, 64, {1.0f, 0.0f});
   StubAudioSource source;

   ArrayOf<bool> map0(2u);
   map0[0] = true;
   map0[1] = false;

   ArrayOf<bool> map1(2u);
   map1[0] = false;
   map1[1] = true;

   const ArrayOf<bool> maps[] = { std::move(map0), std::move(map1) };

   SequenceDownmixSource dmx(source, seq, maps);

   EXPECT_FALSE(dmx.CanMakeMono());
}
