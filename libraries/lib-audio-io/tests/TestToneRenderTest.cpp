/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneRenderTest.cpp

  Unit tests for RenderTestToneInterleaved -- the testable seam
  through which AudioIO's FillTestToneOutputBuffer drives both the
  Direct hardware test mode and the Routing matrix test mode.
  Verifies dispatch, mask interpretation, channel-stride handling,
  and behavioural equivalence between the two modes for the same
  routing.

**********************************************************************/

#include "TestToneRender.h"

#include "PlaybackOutputMask.h"
#include "TestToneGenerator.h"

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kFrames = 1024;

//! Sample of channel @p ch at frame @p i in an interleaved buffer.
inline float Sample(const std::vector<float>& buf,
   std::size_t devChannels, std::size_t i, std::size_t ch)
{
   return buf[devChannels * i + ch];
}

//! Peak absolute value of a single channel slice in an interleaved
//! buffer.
float PeakChannel(const std::vector<float>& buf,
   std::size_t devChannels, std::size_t framesPerBuffer,
   std::size_t ch)
{
   float peak = 0.0f;
   for (std::size_t i = 0; i < framesPerBuffer; ++i)
      peak = std::max(peak,
         std::fabs(Sample(buf, devChannels, i, ch)));
   return peak;
}

//! Sum of squares for a single channel slice -- effective energy
//! measure that picks up signal even at sub-peak levels.
double EnergyChannel(const std::vector<float>& buf,
   std::size_t devChannels, std::size_t framesPerBuffer,
   std::size_t ch)
{
   double sum = 0.0;
   for (std::size_t i = 0; i < framesPerBuffer; ++i) {
      const double s = Sample(buf, devChannels, i, ch);
      sum += s * s;
   }
   return sum;
}

//! Compose a sized request + scratch set for a typical 16-channel
//! device.  Tests reuse this so the boilerplate doesn't dominate.
struct TestFixture {
   TestToneGenerator gen;
   std::vector<float> srcScratch;
   std::vector<std::vector<float>> outScratch;
   std::vector<float*> dstScratch;
   std::vector<float> output;
   std::size_t numPlaybackChannels = 16;
   std::size_t devicePlaybackChannels = 16;

   TestFixture()
   {
      gen.Configure(TestToneGenerator::Type::Sine, 1000.0, 0.0, kRate);
      srcScratch.assign(kFrames, 0.0f);
      outScratch.assign(numPlaybackChannels,
         std::vector<float>(kFrames, 0.0f));
      dstScratch.assign(numPlaybackChannels, nullptr);
      output.assign(devicePlaybackChannels * kFrames, 0.0f);
   }

   TestToneRenderParams MakeParams(
      TestToneRequest::Mode mode, PlaybackOutputMask mask) const
   {
      TestToneRenderParams p;
      p.mode = mode;
      p.mask = mask;
      p.numPlaybackChannels = numPlaybackChannels;
      p.devicePlaybackChannels = devicePlaybackChannels;
      return p;
   }
};

} // namespace

TEST_CASE("RenderTestToneInterleaved: empty mask is a no-op",
   "[testtonerender]")
{
   TestFixture f;
   PlaybackOutputMask empty;
   const bool wrote = RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, empty),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch);

   REQUIRE_FALSE(wrote);
   for (float s : f.output)
      REQUIRE(s == 0.0f);
}

TEST_CASE("RenderTestToneInterleaved: Off mode is a no-op",
   "[testtonerender]")
{
   TestFixture f;
   PlaybackOutputMask mask;
   mask.set(5);
   const bool wrote = RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::Off, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch);

   REQUIRE_FALSE(wrote);
   for (float s : f.output)
      REQUIRE(s == 0.0f);
}

TEST_CASE("RenderTestToneInterleaved: zero frames is a no-op",
   "[testtonerender]")
{
   TestFixture f;
   PlaybackOutputMask mask;
   mask.set(5);
   const bool wrote = RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, /*framesPerBuffer*/ 0, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch);

   REQUIRE_FALSE(wrote);
}

TEST_CASE("RenderTestToneInterleaved: zero playback channels is a no-op",
   "[testtonerender]")
{
   TestFixture f;
   f.numPlaybackChannels = 0;
   PlaybackOutputMask mask;
   mask.set(5);
   const bool wrote = RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch);

   REQUIRE_FALSE(wrote);
}

TEST_CASE("RenderTestToneInterleaved: Direct mode writes to set bits only",
   "[testtonerender]")
{
   TestFixture f;
   PlaybackOutputMask mask;
   mask.set(5);  // user channel 6
   mask.set(10); // user channel 11

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   // Channels 5 and 10 should carry the tone (peak ~1.0 at 0 dBFS).
   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 5)
      > 0.5f);
   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 10)
      > 0.5f);
   // Every other channel should be exactly zero.
   for (std::size_t ch = 0; ch < f.devicePlaybackChannels; ++ch) {
      if (ch == 5 || ch == 10) continue;
      REQUIRE(EnergyChannel(f.output, f.devicePlaybackChannels,
         kFrames, ch) == 0.0);
   }
}

TEST_CASE("RenderTestToneInterleaved: Direct mode ignores bits past device width",
   "[testtonerender]")
{
   TestFixture f;
   // Set a mix of in-range and out-of-range bits.
   PlaybackOutputMask mask;
   mask.set(2);   // in range
   mask.set(20);  // past numPlaybackChannels=16
   mask.set(100); // way past

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   // In-range bit is loud; output buffer is only 16-wide so the
   // out-of-range bits couldn't have been written even with
   // wrong code.  Verify in-range channel.
   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 2)
      > 0.5f);
   for (std::size_t ch = 0; ch < f.devicePlaybackChannels; ++ch) {
      if (ch == 2) continue;
      REQUIRE(EnergyChannel(f.output, f.devicePlaybackChannels,
         kFrames, ch) == 0.0);
   }
}

TEST_CASE("RenderTestToneInterleaved: Matrix mode matches Direct for same mask",
   "[testtonerender]")
{
   // Behavioural-equivalence: for any single-source-channel mask
   // the routing engine should produce the same output as Direct
   // mode.  Divergence here would prove a bug in
   // RouteTrackSamples or its mask interpretation.
   for (auto bits : { std::vector<unsigned>{ 0 },
                      std::vector<unsigned>{ 5 },
                      std::vector<unsigned>{ 0, 1 },
                      std::vector<unsigned>{ 1, 5, 10 },
                      std::vector<unsigned>{ 0, 15 },
                      std::vector<unsigned>{ 3, 7, 11, 15 } })
   {
      PlaybackOutputMask mask;
      for (unsigned b : bits) mask.set(b);

      TestFixture fA, fB;
      // Reset both generators identically so they emit the same
      // tone block (the generator's RNG / phase state is captured
      // at Configure time).
      fA.gen.Reset();
      fA.gen.Configure(TestToneGenerator::Type::Sine,
         1000.0, 0.0, kRate);
      fB.gen.Reset();
      fB.gen.Configure(TestToneGenerator::Type::Sine,
         1000.0, 0.0, kRate);

      REQUIRE(RenderTestToneInterleaved(
         fA.MakeParams(TestToneRequest::Mode::DirectHW, mask),
         fA.gen, kFrames, fA.output.data(),
         fA.srcScratch, fA.outScratch, fA.dstScratch));
      REQUIRE(RenderTestToneInterleaved(
         fB.MakeParams(TestToneRequest::Mode::ThroughMatrix, mask),
         fB.gen, kFrames, fB.output.data(),
         fB.srcScratch, fB.outScratch, fB.dstScratch));

      // Bit-exact equivalence: both modes should produce the
      // identical interleaved buffer.  This is the regression
      // guard that gives Mode B its diagnostic value -- if a
      // bug lands in RouteTrackSamples that reorders bits or
      // rescales source channels, this test will fire.
      INFO("bits = " << bits.size());
      double maxDelta = 0.0;
      for (std::size_t i = 0; i < fA.output.size(); ++i) {
         maxDelta = std::max(maxDelta,
            static_cast<double>(std::fabs(fA.output[i] - fB.output[i])));
      }
      REQUIRE(maxDelta < 1e-6);
   }
}

TEST_CASE("RenderTestToneInterleaved: device stride > playback channels",
   "[testtonerender]")
{
   // ALSA case: device opened wide (24 channels) to defeat the
   // PortAudio plugin's channel-adaption logic, but the user is
   // playing back only 16 channels.  Output stride is 24 and the
   // last 8 columns of every frame should remain zero.
   TestFixture f;
   f.numPlaybackChannels = 16;
   f.devicePlaybackChannels = 24;
   f.outScratch.assign(f.numPlaybackChannels,
      std::vector<float>(kFrames, 0.0f));
   f.dstScratch.assign(f.numPlaybackChannels, nullptr);
   f.output.assign(f.devicePlaybackChannels * kFrames, 0.0f);

   PlaybackOutputMask mask;
   mask.set(0);
   mask.set(15); // last reachable bit

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 0)
      > 0.5f);
   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 15)
      > 0.5f);
   // Past-device-width slots in every frame should be untouched.
   for (std::size_t ch = 16; ch < f.devicePlaybackChannels; ++ch) {
      REQUIRE(EnergyChannel(f.output, f.devicePlaybackChannels,
         kFrames, ch) == 0.0);
   }
}

TEST_CASE("RenderTestToneInterleaved: writes are additive (mixes with prior content)",
   "[testtonerender]")
{
   // The render function uses += semantics so callers can mix
   // the test tone with playthrough or other signals.  Verify
   // by pre-filling the output buffer with a known constant and
   // checking the tone is added on top, not over-writing.
   TestFixture f;
   const float kPreFill = 0.25f;
   std::fill(f.output.begin(), f.output.end(), kPreFill);

   PlaybackOutputMask mask;
   mask.set(3);

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   // Channel 3: tone added to 0.25, so peak should be near
   // 1.0 + 0.25 = 1.25 (or near -1.0 + 0.25 = -0.75 -- check
   // both via Peak).
   const float ch3Peak = PeakChannel(f.output, f.devicePlaybackChannels,
      kFrames, 3);
   REQUIRE(ch3Peak > 0.7f);
   REQUIRE(ch3Peak < 1.3f);
   // Other channels: still at the pre-fill value, untouched.
   for (std::size_t ch = 0; ch < f.devicePlaybackChannels; ++ch) {
      if (ch == 3) continue;
      // Peak of a constant signal is the constant itself.
      const float p = PeakChannel(f.output, f.devicePlaybackChannels,
         kFrames, ch);
      REQUIRE(p == Approx(kPreFill).margin(1e-6f));
   }
}

TEST_CASE("RenderTestToneInterleaved: small framesPerBuffer (single sample)",
   "[testtonerender]")
{
   // ASIO and some WASAPI configurations can hand the callback
   // very small blocks.  Verify the routine works at the smallest
   // possible block size (1 frame) -- catches off-by-one errors
   // in the inner loops that pass-larger-block tests would miss.
   TestFixture f;
   PlaybackOutputMask mask;
   mask.set(7);

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, /*framesPerBuffer*/ 1, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   // Check the single sample at channel 7 is set; everything else
   // (including frame 1+) is zero from the initial assign().
   for (std::size_t ch = 0; ch < f.devicePlaybackChannels; ++ch) {
      if (ch == 7) continue;
      REQUIRE(f.output[ch] == 0.0f);
   }
   // Channel 7 frame 0 holds the tone's sample 0.  At 1 kHz / 48 kHz,
   // sample 0 of the freshly-Configured sine is sin(0) = 0.0.
   REQUIRE(f.output[7] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("RenderTestToneInterleaved: Matrix mode handles undersized scratch",
   "[testtonerender]")
{
   // The fall-back resize path: caller under-sized the scratch
   // (production callers in AudioIO pre-size, but tests / future
   // callers might not).  Function should grow scratch and produce
   // correct output rather than read out of bounds.
   TestFixture f;
   // Strip outScratch and dstScratch so the function has to grow
   // them.  srcScratch left at kFrames so we can still verify
   // output content.
   f.outScratch.clear();
   f.dstScratch.clear();
   PlaybackOutputMask mask;
   mask.set(2);
   mask.set(8);

   REQUIRE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::ThroughMatrix, mask),
      f.gen, kFrames, f.output.data(),
      f.srcScratch, f.outScratch, f.dstScratch));

   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 2)
      > 0.5f);
   REQUIRE(PeakChannel(f.output, f.devicePlaybackChannels, kFrames, 8)
      > 0.5f);
}

TEST_CASE("RenderTestToneInterleaved: returns false for null output",
   "[testtonerender]")
{
   TestFixture f;
   PlaybackOutputMask mask;
   mask.set(1);
   REQUIRE_FALSE(RenderTestToneInterleaved(
      f.MakeParams(TestToneRequest::Mode::DirectHW, mask),
      f.gen, kFrames, /*outputInterleaved*/ nullptr,
      f.srcScratch, f.outScratch, f.dstScratch));
}
