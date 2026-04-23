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

#include <cstdint>
#include <memory>

namespace
{
constexpr int kSampleRate = 44100;
constexpr size_t kNumChannels = 1;

//! Build a StretchingSequence wrapping a mock, with a harmless factory so
//! the constructor is happy. Tests focus exclusively on the mask delegation.
//! Returned as a unique_ptr because StretchingSequence holds reference and
//! const-unique_ptr members that preclude copy/move.
std::unique_ptr<StretchingSequence>
MakeSut(const MockPlayableSequence& mock)
{
   return std::make_unique<StretchingSequence>(
      mock, kSampleRate, kNumChannels,
      std::unique_ptr<AudioSegmentFactoryInterface>(new MockAudioSegmentFactory));
}
} // namespace

TEST_CASE(
   "StretchingSequence forwards a non-zero playback output mask",
   "[StretchingSequence][OutputMask]")
{
   // This is the exact bug that regressed: the wrapped track had a mask set
   // via the Playback Routing dialog, but the StretchingSequence decorator
   // did not override GetPlaybackOutputMask() and returned the
   // PlayableSequence base-class default of 0, hiding the mask from AudioIO.
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   mock.playbackOutputMask = 0xDEADBEEFull;

   const auto sut = MakeSut(mock);
   REQUIRE(sut->GetPlaybackOutputMask() == 0xDEADBEEFull);
}

TEST_CASE(
   "StretchingSequence forwards the default (zero) mask",
   "[StretchingSequence][OutputMask]")
{
   // Sanity: if the wrapped sequence has no mask, the decorator must also
   // report 0 -- not some stale or uninitialized value.
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   REQUIRE(mock.playbackOutputMask == 0);

   const auto sut = MakeSut(mock);
   REQUIRE(sut->GetPlaybackOutputMask() == 0);
}

TEST_CASE(
   "StretchingSequence reflects live mask changes on the wrapped sequence",
   "[StretchingSequence][OutputMask]")
{
   // Verifies the decorator forwards *live* -- it does not cache the mask
   // at construction time. The user can change the mask via the UI after
   // playback prep has wrapped the track.
   MockPlayableSequence mock(kSampleRate, kNumChannels);
   const auto sut = MakeSut(mock);

   REQUIRE(sut->GetPlaybackOutputMask() == 0);

   mock.playbackOutputMask = 0x5;
   REQUIRE(sut->GetPlaybackOutputMask() == 0x5);

   mock.playbackOutputMask = 1ull << 40;
   REQUIRE(sut->GetPlaybackOutputMask() == (1ull << 40));

   mock.playbackOutputMask = 0;
   REQUIRE(sut->GetPlaybackOutputMask() == 0);
}
