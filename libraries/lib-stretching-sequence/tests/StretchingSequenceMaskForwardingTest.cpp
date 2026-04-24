/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  StretchingSequenceMaskForwardingTest.cpp

  Regression tests for the bug where StretchingSequence failed to
  forward GetPlaybackOutputMask() to the wrapped PlayableSequence. In
  practice the wrapped sequence is a WaveTrack, and AudioIO queries the
  mask off the StretchingSequence decorator -- so without forwarding,
  user-set routing masks were silently lost during playback.

**********************************************************************/
#include "MockAudioSegmentFactory.h"
#include "MockPlayableSequence.h"
#include "StretchingSequence.h"

#include <catch2/catch.hpp>

#include <memory>

namespace
{
constexpr int kSampleRate = 44100;
constexpr size_t kNumChannels = 1;

std::unique_ptr<StretchingSequence>
MakeSut(const MockPlayableSequence& mock)
{
   return std::make_unique<StretchingSequence>(
      mock, kSampleRate, kNumChannels,
      std::unique_ptr<AudioSegmentFactoryInterface>(
         new MockAudioSegmentFactory));
}
} // namespace

TEST_CASE(
   "StretchingSequence forwards a non-empty playback output mask",
   "[StretchingSequence][OutputMask]")
{
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   mock.playbackOutputMask = { 0xDEADBEEFull, 0 };

   const auto sut = MakeSut(mock);
   REQUIRE(sut->GetPlaybackOutputMask() ==
      PlaybackOutputMask{ 0xDEADBEEFull, 0 });
}

TEST_CASE(
   "StretchingSequence forwards the default (empty) mask",
   "[StretchingSequence][OutputMask]")
{
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   REQUIRE(mock.playbackOutputMask.empty());

   const auto sut = MakeSut(mock);
   REQUIRE(sut->GetPlaybackOutputMask().empty());
}

TEST_CASE(
   "StretchingSequence reflects live mask changes on the wrapped sequence",
   "[StretchingSequence][OutputMask]")
{
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   const auto sut = MakeSut(mock);

   REQUIRE(sut->GetPlaybackOutputMask().empty());

   mock.playbackOutputMask = { 0x5, 0 };
   REQUIRE(sut->GetPlaybackOutputMask() == PlaybackOutputMask{ 0x5, 0 });

   // Cross-boundary bits: bit 64 lives in the high word.
   mock.playbackOutputMask = {};
   mock.playbackOutputMask.set(64);
   REQUIRE(sut->GetPlaybackOutputMask().test(64));
   REQUIRE_FALSE(sut->GetPlaybackOutputMask().test(63));

   mock.playbackOutputMask = {};
   REQUIRE(sut->GetPlaybackOutputMask().empty());
}
