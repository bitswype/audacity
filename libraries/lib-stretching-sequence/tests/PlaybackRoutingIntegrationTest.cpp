/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingIntegrationTest.cpp

  End-to-end test of the per-track playback routing pipeline, using
  the real WaveTrack, real StretchingSequence, real
  ComputeChannelAssignments, and real RouteTrackSamples.

  The individual layer tests cover their own behavior; this file
  proves the layers compose correctly across the static-mask refactor.

**********************************************************************/
#include "MockSampleBlockFactory.h"
#include "TestWaveClipMaker.h"
#include "TestWaveTrackMaker.h"

#include "ChannelRouting.h"
#include "PlaybackOutputMask.h"
#include "RouteTrackSamples.h"
#include "StretchingSequence.h"
#include "WaveTrack.h"

#include <catch2/catch.hpp>

#include <array>
#include <memory>
#include <vector>

namespace
{
constexpr int kSampleRate = 44100;

struct PipelineRunner
{
   struct TrackInput {
      std::shared_ptr<WaveTrack> track;
      std::vector<std::vector<float>> sourceSamples;
      PlaybackOutputMask mask{};
   };

   static std::vector<std::vector<float>>
   Run(std::vector<TrackInput>& inputs, size_t numOutputChannels,
       size_t samplesPerTrack)
   {
      std::vector<std::shared_ptr<StretchingSequence>> sequences;
      sequences.reserve(inputs.size());
      for (auto& input : inputs) {
         input.track->SetPlaybackOutputMask(input.mask);
         sequences.push_back(StretchingSequence::Create(
            *input.track, input.track->GetClipInterfaces()));
      }

      // Mirror AudioIO::StartStream: snapshot per-track masks and
      // compute assignments.
      std::vector<PlaybackOutputMask> masks;
      masks.reserve(sequences.size());
      for (const auto& seq : sequences)
         masks.push_back(seq->GetPlaybackOutputMask());
      const auto assignments = ComputeChannelAssignments(masks);

      // Mirror AudioIO::ProcessPlaybackSlices.
      std::vector<std::vector<float>> masterStorage(
         numOutputChannels, std::vector<float>(samplesPerTrack, 0.f));
      std::vector<float*> masterPtrs(numOutputChannels);
      for (size_t i = 0; i < numOutputChannels; ++i)
         masterPtrs[i] = masterStorage[i].data();

      for (size_t t = 0; t < inputs.size(); ++t) {
         auto procStorage = inputs[t].sourceSamples;
         std::vector<float*> procPtrs(procStorage.size());
         for (size_t c = 0; c < procStorage.size(); ++c)
            procPtrs[c] = procStorage[c].data();

         const auto& seq = sequences[t];
         RouteTrackSamples(
            assignments[t],
            seq->NChannels(),
            numOutputChannels,
            samplesPerTrack,
            [&seq](int ch) { return seq->GetChannelVolume(ch); },
            procPtrs.data(),
            masterPtrs.data());
      }

      return masterStorage;
   }
};

struct Makers {
   SampleBlockFactoryPtr factory {
      std::make_shared<MockSampleBlockFactory>() };
   TestWaveClipMaker clipMaker { kSampleRate, factory };
   TestWaveTrackMaker trackMaker { kSampleRate, factory };
};

Makers& MakersInstance()
{
   static Makers m;
   return m;
}

std::shared_ptr<WaveTrack>
MakeMonoTrack(float value, size_t samples)
{
   auto& m = MakersInstance();
   auto clip = m.clipMaker.ClipFilledWith(value, samples, 1);
   return m.trackMaker.Track(clip);
}

PlaybackOutputMask BitMask(unsigned bit)
{
   PlaybackOutputMask m;
   m.set(bit);
   return m;
}

} // namespace

TEST_CASE(
   "Pipeline: mono mask reaches master buffer via StretchingSequence",
   "[Integration][Routing]")
{
   // Regression for the field-visible bug: a mask set on a WaveTrack
   // was being dropped by StretchingSequence's missing
   // GetPlaybackOutputMask override.
   const size_t samples = 64;
   const float value = 1.f;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(value, samples),
      { std::vector<float>(samples, value) },
      BitMask(0),
   });

   const auto master = PipelineRunner::Run(inputs, 4, samples);

   REQUIRE(master[0][0] == Catch::Detail::Approx(value));
   REQUIRE(master[1][0] == 0.f);
   REQUIRE(master[2][0] == 0.f);
   REQUIRE(master[3][0] == 0.f);
}

TEST_CASE(
   "Pipeline: four mono tracks masked to output 0 sum into channel 0",
   "[Integration][Routing]")
{
   // Exactly the scenario Chris reported from hardware: four mono
   // tracks each masked to the left channel only.  All four tracks
   // land in master[0], master[1] stays silent.
   const size_t samples = 32;

   std::vector<PipelineRunner::TrackInput> inputs;
   const std::array<float, 4> values = { 1.f, 2.f, 3.f, 4.f };
   for (float v : values)
      inputs.push_back({
         MakeMonoTrack(v, samples),
         { std::vector<float>(samples, v) },
         BitMask(0),
      });

   const auto master = PipelineRunner::Run(inputs, 2, samples);

   REQUIRE(master[0][0] == Catch::Detail::Approx(10.f));
   REQUIRE(master[1][0] == 0.f);
}

TEST_CASE(
   "Pipeline: empty mask silences the track end-to-end",
   "[Integration][Routing]")
{
   // Empty mask means silent in the static-mask model.  A track with
   // no bits set must produce no audio anywhere, while neighboring
   // tracks route normally.
   const size_t samples = 16;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(1.f, samples),
      { std::vector<float>(samples, 1.f) },
      {}, // empty mask -> silent
   });
   PlaybackOutputMask bothBits;
   bothBits.set(0);
   bothBits.set(1);
   inputs.push_back({
      MakeMonoTrack(4.f, samples),
      { std::vector<float>(samples, 4.f) },
      bothBits,
   });

   const auto master = PipelineRunner::Run(inputs, 2, samples);

   // Only the non-empty track contributes; the silent track is gone.
   REQUIRE(master[0][0] == Catch::Detail::Approx(4.f));
   REQUIRE(master[1][0] == Catch::Detail::Approx(4.f));
}

TEST_CASE(
   "Pipeline: mask routes 16 mono tracks across a 16-channel device",
   "[Integration][Routing]")
{
   // Shaker-style test: 16 mono tracks, each masked to a distinct
   // output channel in an arbitrary permutation.  Each output
   // receives exactly one track.
   const size_t samples = 8;
   constexpr size_t numOut = 16;

   std::vector<PipelineRunner::TrackInput> inputs;
   for (size_t i = 0; i < numOut; ++i) {
      const float v = static_cast<float>(i + 1);
      const unsigned outCh = static_cast<unsigned>(numOut - 1 - i);
      inputs.push_back({
         MakeMonoTrack(v, samples),
         { std::vector<float>(samples, v) },
         BitMask(outCh),
      });
   }

   const auto master = PipelineRunner::Run(inputs, numOut, samples);

   for (size_t outCh = 0; outCh < numOut; ++outCh) {
      const float expected = static_cast<float>(numOut - outCh);
      CAPTURE(outCh);
      REQUIRE(master[outCh][0] == Catch::Detail::Approx(expected));
   }
}

TEST_CASE(
   "Pipeline: changing a mask after wrapping is observed by playback",
   "[Integration][Routing]")
{
   // StretchingSequence wraps the WaveTrack by reference.  Changing
   // the mask on the track after wrapping must be visible to the
   // next playback callback.
   const size_t samples = 16;
   auto track = MakeMonoTrack(1.f, samples);
   track->SetPlaybackOutputMask(BitMask(0));

   auto seq = StretchingSequence::Create(
      *track, track->GetClipInterfaces());
   REQUIRE(seq->GetPlaybackOutputMask() == BitMask(0));

   track->SetPlaybackOutputMask(BitMask(1));
   REQUIRE(seq->GetPlaybackOutputMask() == BitMask(1));

   track->SetPlaybackOutputMask({});
   REQUIRE(seq->GetPlaybackOutputMask().empty());
}

TEST_CASE(
   "Pipeline: mask on a high-word bit (above 64) routes correctly",
   "[Integration][Routing]")
{
   // Covers the 128-bit widening: bit 70 on a 128-output device must
   // reach master[70].
   const size_t samples = 8;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(3.f, samples),
      { std::vector<float>(samples, 3.f) },
      BitMask(70),
   });

   const auto master = PipelineRunner::Run(inputs, 128, samples);

   REQUIRE(master[63][0] == 0.f);
   REQUIRE(master[69][0] == 0.f);
   REQUIRE(master[70][0] == Catch::Detail::Approx(3.f));
   REQUIRE(master[71][0] == 0.f);
}
