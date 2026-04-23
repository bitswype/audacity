/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingIntegrationTest.cpp

  End-to-end test of the per-track playback routing pipeline, using
  the real WaveTrack, real StretchingSequence, real
  ComputeChannelAssignments, and real RouteTrackSamples.

  Motivation: the unit tests for each piece (ChannelRouting,
  StretchingSequence mask forwarding, RouteTrackSamples) all passed,
  but field testing showed the feature still not working.  The gap
  between the unit tests and reality was the composition: a mask set
  on a WaveTrack has to survive every layer of the real pipeline.
  Individual layer tests don't catch it when a layer silently drops
  the value.

  This file simulates what AudioIO::StartStream and
  ProcessPlaybackSlices do, without going through PortAudio.

**********************************************************************/
#include "MockSampleBlockFactory.h"
#include "TestWaveClipMaker.h"
#include "TestWaveTrackMaker.h"

#include "ChannelRouting.h" // also provides kPlaybackRoutingSilentSentinel
#include "RouteTrackSamples.h"
#include "StretchingSequence.h"
#include "WaveTrack.h"

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
constexpr int kSampleRate = 44100;

//! Build a playback pipeline state equivalent to what AudioIO would
//! hold after StartStream, then run one track's samples through
//! ComputeChannelAssignments + RouteTrackSamples.
struct PipelineRunner
{
   //! Per-track input: source samples (one vector per source channel)
   //! and an optional mask set on the track.  If @c mask is 0, the
   //! track uses auto routing.
   struct TrackInput {
      std::shared_ptr<WaveTrack> track;
      std::vector<std::vector<float>> sourceSamples;
      uint64_t mask = 0;
   };

   //! Run the whole pipeline and return the resulting master buffers
   //! (one vector per output channel).  @c samplesPerTrack must match
   //! the length of each sourceSamples[] entry.
   static std::vector<std::vector<float>>
   Run(std::vector<TrackInput>& inputs, size_t numOutputChannels,
       size_t samplesPerTrack)
   {
      // 1. Apply each mask to its track, then wrap in the real
      //    StretchingSequence decorator just like
      //    TransportUtilities::MakeTransportTracks does.
      std::vector<std::shared_ptr<StretchingSequence>> sequences;
      sequences.reserve(inputs.size());
      for (auto& input : inputs) {
         input.track->SetPlaybackOutputMask(input.mask);
         sequences.push_back(StretchingSequence::Create(
            *input.track, input.track->GetClipInterfaces()));
      }

      // 2. Mirror AudioIO::StartStream: collect per-track channel
      //    counts and masks, and compute assignments.
      std::vector<size_t> channelCounts;
      std::vector<uint64_t> masks;
      channelCounts.reserve(sequences.size());
      masks.reserve(sequences.size());
      for (const auto& seq : sequences) {
         channelCounts.push_back(seq->NChannels());
         masks.push_back(seq->GetPlaybackOutputMask());
      }
      const auto assignments = ComputeChannelAssignments(
         channelCounts, masks, numOutputChannels);

      // 3. Mirror AudioIO::ProcessPlaybackSlices: set up master
      //    buffers and per-track processing buffers, invoke the
      //    real RouteTrackSamples.
      std::vector<std::vector<float>> masterStorage(
         numOutputChannels, std::vector<float>(samplesPerTrack, 0.f));
      std::vector<float*> masterPtrs(numOutputChannels);
      for (size_t i = 0; i < numOutputChannels; ++i)
         masterPtrs[i] = masterStorage[i].data();

      for (size_t t = 0; t < inputs.size(); ++t) {
         // Fresh processing buffers for each track (cases 2 and 3
         // of RouteTrackSamples mutate them in place).
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

//! Bind a factory + makers with static-local storage so every TEST_CASE
//! that uses them sees the same factory instance.  (Consistent with
//! the existing tests in this directory.)
struct Makers {
   SampleBlockFactoryPtr factory { std::make_shared<MockSampleBlockFactory>() };
   TestWaveClipMaker clipMaker { kSampleRate, factory };
   TestWaveTrackMaker trackMaker { kSampleRate, factory };
};

Makers& MakersInstance()
{
   static Makers m;
   return m;
}

//! Build a mono WaveTrack whose single clip contains @c samples at the
//! given constant value.  The track is ready to have a mask set on it.
std::shared_ptr<WaveTrack>
MakeMonoTrack(float value, size_t samples)
{
   auto& m = MakersInstance();
   auto clip = m.clipMaker.ClipFilledWith(value, samples, 1);
   return m.trackMaker.Track(clip);
}

} // namespace

TEST_CASE(
   "Pipeline: mono mask reaches master buffer via StretchingSequence",
   "[Integration][Routing]")
{
   // Regression for the field-visible bug: a mask set on a WaveTrack
   // was being dropped by StretchingSequence's missing
   // GetPlaybackOutputMask override.  This test fails if any layer of
   // the real pipeline (track storage, decorator forwarding,
   // assignment computation, sample distribution) silently drops the
   // mask.
   const size_t samples = 64;
   const float value = 1.f;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(value, samples),
      { std::vector<float>(samples, value) },
      0b0001, // only output 0
   });

   const auto master = PipelineRunner::Run(inputs, /*numOutputChannels=*/4, samples);

   REQUIRE(master[0][0] == Catch::Detail::Approx(value));
   REQUIRE(master[1][0] == 0.f);
   REQUIRE(master[2][0] == 0.f);
   REQUIRE(master[3][0] == 0.f);
}

TEST_CASE(
   "Pipeline: four mono tracks masked to output 0 all sum into channel 0",
   "[Integration][Routing]")
{
   // Exactly the scenario Chris reported: four mono tracks each
   // masked to the left channel only.  On a stereo device
   // (numOutputChannels == 2) all four tracks must land in master[0],
   // and master[1] must stay silent.
   const size_t samples = 32;

   std::vector<PipelineRunner::TrackInput> inputs;
   const std::array<float, 4> values = { 1.f, 2.f, 3.f, 4.f };
   for (float v : values)
      inputs.push_back({
         MakeMonoTrack(v, samples),
         { std::vector<float>(samples, v) },
         0b0001,
      });

   const auto master = PipelineRunner::Run(inputs, /*numOutputChannels=*/2, samples);

   // Master 0 = 1 + 2 + 3 + 4 = 10
   REQUIRE(master[0][0] == Catch::Detail::Approx(10.f));
   // Master 1 must be silent: the failing behavior was master[1]
   // receiving audio when the user masked tracks to left-only.
   REQUIRE(master[1][0] == 0.f);
}

TEST_CASE(
   "Pipeline: mask 0 keeps legacy auto-routing behavior intact",
   "[Integration][Routing]")
{
   // Two mono tracks, no masks, stereo device.  The pre-routing-matrix
   // behavior is "mono tracks duplicate to every output channel".
   // This guards against the new mask plumbing accidentally changing
   // the default behavior.
   const size_t samples = 16;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(1.f, samples),
      { std::vector<float>(samples, 1.f) },
      0,
   });
   inputs.push_back({
      MakeMonoTrack(2.f, samples),
      { std::vector<float>(samples, 2.f) },
      0,
   });

   const auto master = PipelineRunner::Run(inputs, /*numOutputChannels=*/2, samples);

   // Each track duplicates to both outputs; masters are the sum.
   REQUIRE(master[0][0] == Catch::Detail::Approx(3.f));
   REQUIRE(master[1][0] == Catch::Detail::Approx(3.f));
}

TEST_CASE(
   "Pipeline: mask routes track N to output M on a 16-channel device",
   "[Integration][Routing]")
{
   // Shaker-style test: 16 mono tracks, each masked to a distinct
   // output channel in an arbitrary permutation, on a 16-output
   // device.  Each output must receive exactly one track.
   const size_t samples = 8;
   constexpr size_t numOut = 16;

   // Permutation: track i routes to output (15 - i)
   std::vector<PipelineRunner::TrackInput> inputs;
   for (size_t i = 0; i < numOut; ++i) {
      const float v = static_cast<float>(i + 1);
      const size_t outCh = numOut - 1 - i;
      inputs.push_back({
         MakeMonoTrack(v, samples),
         { std::vector<float>(samples, v) },
         uint64_t(1) << outCh,
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
   "Pipeline: explicit-silent sentinel silences a track end-to-end",
   "[Integration][Routing]")
{
   // Simulates the dialog's "user unchecked every box on this row"
   // case.  A track with mask = kPlaybackRoutingSilentSentinel must
   // produce no audio anywhere, while neighboring tracks route
   // normally.  Distinct from mask = 0 (auto), which on a stereo
   // device duplicates mono across both outputs.
   const size_t samples = 16;

   std::vector<PipelineRunner::TrackInput> inputs;
   inputs.push_back({
      MakeMonoTrack(1.f, samples),
      { std::vector<float>(samples, 1.f) },
      kPlaybackRoutingSilentSentinel, // silenced
   });
   inputs.push_back({
      MakeMonoTrack(4.f, samples),
      { std::vector<float>(samples, 4.f) },
      0, // auto -> duplicates to both outputs on a stereo device
   });

   const auto master = PipelineRunner::Run(inputs, /*numOutputChannels=*/2, samples);

   // Only the auto track contributes; silenced track is gone.
   REQUIRE(master[0][0] == Catch::Detail::Approx(4.f));
   REQUIRE(master[1][0] == Catch::Detail::Approx(4.f));
}

TEST_CASE(
   "Pipeline: changing a mask after wrapping is observed by playback",
   "[Integration][Routing]")
{
   // StretchingSequence wraps the WaveTrack by reference.  Changing
   // the mask on the track after wrapping MUST be visible to the
   // next playback callback.  This is the dialog-apply workflow:
   // user edits masks, then presses Play without recreating the
   // sequences.  (In practice StartStream rebuilds sequences, but
   // the decorator must still not cache.)
   const size_t samples = 16;
   auto track = MakeMonoTrack(1.f, samples);
   track->SetPlaybackOutputMask(0b0001);

   auto seq = StretchingSequence::Create(*track, track->GetClipInterfaces());
   REQUIRE(seq->GetPlaybackOutputMask() == 0b0001);

   // User opens the dialog and re-routes to output 1.
   track->SetPlaybackOutputMask(0b0010);
   REQUIRE(seq->GetPlaybackOutputMask() == 0b0010);

   // User clears routing.
   track->SetPlaybackOutputMask(0);
   REQUIRE(seq->GetPlaybackOutputMask() == 0);
}
