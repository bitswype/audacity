/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneGeneratorTest.cpp

  Unit tests for the bitswype fork's TestToneGenerator.

**********************************************************************/

#include "TestToneGenerator.h"

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

//! Linear amplitude for a given dBFS level.
double LevelToLinear(double db) { return std::pow(10.0, db / 20.0); }

//! Peak absolute value of @p buf.
float PeakAbs(const std::vector<float>& buf)
{
   float peak = 0.0f;
   for (float s : buf)
      peak = std::max(peak, std::fabs(s));
   return peak;
}

//! Compute the normalized dot-product correlation between @p signal
//! and a sine wave of @p freqHz.  Used as a poor-man's frequency
//! detector that doesn't require pulling in an FFT.  Returns a
//! magnitude in [0, ~1].
double CorrelateSine(const std::vector<float>& signal,
   double freqHz, double sampleRate)
{
   double sumSin = 0.0;
   double sumCos = 0.0;
   for (size_t i = 0; i < signal.size(); ++i) {
      const double phase = 2.0 * kPi * freqHz * (double)i / sampleRate;
      sumSin += signal[i] * std::sin(phase);
      sumCos += signal[i] * std::cos(phase);
   }
   const double mag = std::sqrt(sumSin * sumSin + sumCos * sumCos);
   return mag * 2.0 / signal.size();
}

//! RMS of the buffer.
double Rms(const std::vector<float>& buf)
{
   if (buf.empty()) return 0.0;
   double sum = 0.0;
   for (float s : buf) sum += static_cast<double>(s) * s;
   return std::sqrt(sum / buf.size());
}

} // namespace

TEST_CASE("TestToneGenerator: Sine produces correct amplitude at -20 dBFS",
   "[testtone]")
{
   constexpr double kRate = 48000.0;
   constexpr double kFreq = 1000.0;
   constexpr double kLevelDb = -20.0;

   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, kFreq, kLevelDb, kRate);

   std::vector<float> buf(static_cast<size_t>(kRate));
   gen.Render(buf.data(), buf.size());

   const float peak = PeakAbs(buf);
   const double expected = LevelToLinear(kLevelDb);
   // Allow 1 % slack for sample-grid quantisation of the sine peak.
   REQUIRE(peak == Approx(expected).margin(expected * 0.01));
}

TEST_CASE("TestToneGenerator: Sine peaks at unity for 0 dBFS",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, 1000.0, 0.0, 48000.0);

   std::vector<float> buf(48000);
   gen.Render(buf.data(), buf.size());
   REQUIRE(PeakAbs(buf) == Approx(1.0f).margin(0.01f));
}

TEST_CASE("TestToneGenerator: Sine emits silence below the dBFS floor",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, 1000.0, -250.0, 48000.0);

   std::vector<float> buf(2048);
   gen.Render(buf.data(), buf.size());
   REQUIRE(PeakAbs(buf) == 0.0f);
}

TEST_CASE("TestToneGenerator: Sine matches its configured frequency",
   "[testtone]")
{
   constexpr double kRate = 48000.0;

   for (double freq : { 50.0, 440.0, 1000.0, 5000.0, 12000.0 }) {
      TestToneGenerator gen;
      gen.Configure(TestToneGenerator::Type::Sine, freq, 0.0, kRate);
      std::vector<float> buf(static_cast<size_t>(kRate));
      gen.Render(buf.data(), buf.size());
      const double expected = LevelToLinear(0.0); // peak ~1
      const double mag = CorrelateSine(
         std::vector<float>(buf.begin() + 1024, buf.end() - 1024),
         freq, kRate);
      // A pure sine should hit ~1.0 against its own frequency.
      // 0.95 is tight enough to fail anything but tiny phase /
      // windowing slack, where the prior 0.7 bound would have
      // passed amplitude-rolloff or shape bugs (e.g. a sawtooth
      // at the right period).
      INFO("freq=" << freq << " mag=" << mag
         << " expected~" << expected);
      REQUIRE(mag > 0.95);
      // And nothing at, say, 7x the configured frequency.  A real
      // sine puts <0.05 of its energy into a non-harmonic
      // off-frequency probe.
      const double bogus = CorrelateSine(
         std::vector<float>(buf.begin() + 1024, buf.end() - 1024),
         freq * 7.13, kRate);
      INFO("bogus=" << bogus);
      REQUIRE(bogus < 0.05);
   }
}

TEST_CASE("TestToneGenerator: Sine phase is continuous across Render calls",
   "[testtone]")
{
   constexpr double kRate = 48000.0;
   constexpr double kFreq = 997.0; // not a nice rate divisor

   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, kFreq, 0.0, kRate);

   // Render in 100 chunks of 480 frames each, vs. one chunk of 48000.
   std::vector<float> chunked(48000), oneShot(48000);
   for (size_t i = 0; i < 100; ++i)
      gen.Render(chunked.data() + i * 480, 480);

   TestToneGenerator gen2;
   gen2.Configure(TestToneGenerator::Type::Sine, kFreq, 0.0, kRate);
   gen2.Render(oneShot.data(), oneShot.size());

   // Each chunk's phase should pick up where the last left off, so
   // chunked == oneShot exactly.
   double maxDelta = 0.0;
   for (size_t i = 0; i < chunked.size(); ++i)
      maxDelta = std::max(maxDelta,
         std::fabs(static_cast<double>(chunked[i]) - oneShot[i]));
   REQUIRE(maxDelta < 1e-5);
}

TEST_CASE("TestToneGenerator: Sine above Nyquist emits silence",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine,
      /*freq*/ 30000.0, /*levelDb*/ 0.0, /*rate*/ 48000.0);
   REQUIRE(gen.ExceedsNyquist());
   std::vector<float> buf(2048);
   gen.Render(buf.data(), buf.size());
   REQUIRE(PeakAbs(buf) == 0.0f);
}

TEST_CASE("TestToneGenerator: Sine at exactly Nyquist is silenced",
   "[testtone]")
{
   // sin(phi + k*pi) alternates +/- with no audible signal -- a
   // generator that didn't short-circuit here would emit a
   // degenerate two-sample square at full level.
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine,
      /*freq*/ 24000.0, /*levelDb*/ 0.0, /*rate*/ 48000.0);
   REQUIRE(gen.ExceedsNyquist());
   std::vector<float> buf(2048);
   gen.Render(buf.data(), buf.size());
   REQUIRE(PeakAbs(buf) == 0.0f);
}

TEST_CASE("TestToneGenerator: Pink noise has nonzero RMS and bounded peak",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Pink, 0.0, -10.0, 48000.0);

   std::vector<float> buf(48000);
   gen.Render(buf.data(), buf.size());

   const double rms = Rms(buf);
   REQUIRE(rms > 0.0);
   // Peak should be reasonably close to the configured peak amplitude.
   // Pink noise has crest factor roughly 3-4, so peak < ~4 * RMS.
   // -10 dBFS amplitude scales the [-1,+1] base output by 0.316.
   const float peak = PeakAbs(buf);
   REQUIRE(peak <= LevelToLinear(-10.0) * 1.25f);
   REQUIRE(peak > 0.05f);
}

TEST_CASE("TestToneGenerator: Pink mean is near zero across a long run",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Pink, 0.0, 0.0, 48000.0);
   std::vector<float> buf(192000);
   gen.Render(buf.data(), buf.size());
   const double mean =
      std::accumulate(buf.begin(), buf.end(), 0.0) / buf.size();
   // Voss pink isn't strictly zero-mean per block, but the long-run
   // mean should be well within a few percent of zero.
   REQUIRE(std::fabs(mean) < 0.05);
}

TEST_CASE("TestToneGenerator: White noise covers a wide range",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::White, 0.0, 0.0, 48000.0);
   std::vector<float> buf(48000);
   gen.Render(buf.data(), buf.size());

   // White noise should hit close to both extremes when rendered for
   // a full second at 0 dBFS -- this distinguishes it from a stuck
   // generator / DC value.
   float minS = *std::min_element(buf.begin(), buf.end());
   float maxS = *std::max_element(buf.begin(), buf.end());
   REQUIRE(minS < -0.5f);
   REQUIRE(maxS > 0.5f);
   const double rms = Rms(buf);
   // Uniform [-1,+1] expected RMS = 1/sqrt(3) ~= 0.577.
   REQUIRE(rms == Approx(0.577).margin(0.05));
}

TEST_CASE("TestToneGenerator: Reset is reproducible",
   "[testtone]")
{
   TestToneGenerator a;
   a.Configure(TestToneGenerator::Type::Pink, 0.0, 0.0, 48000.0);
   a.Reset();
   std::vector<float> bufA(2048);
   a.Render(bufA.data(), bufA.size());

   TestToneGenerator b;
   b.Configure(TestToneGenerator::Type::Pink, 0.0, 0.0, 48000.0);
   b.Reset();
   std::vector<float> bufB(2048);
   b.Render(bufB.data(), bufB.size());

   REQUIRE(bufA == bufB);
}

TEST_CASE("TestToneGenerator: Configure with bad sample rate falls back gracefully",
   "[testtone]")
{
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, 1000.0, 0.0, /*rate*/ 0.0);
   std::vector<float> buf(1024);
   // Render must not crash or NaN even when rate is invalid; the
   // generator falls back to 48000 internally.
   gen.Render(buf.data(), buf.size());
   for (float s : buf) {
      REQUIRE(std::isfinite(s));
   }
}

TEST_CASE("TestToneGenerator: Mode switch glitch-free without Reset",
   "[testtone]")
{
   // Mid-buffer reconfiguration from sine to pink should keep
   // rendering without throwing or producing NaN -- a stand-in for
   // the "user changes type while tone is playing" scenario in the
   // dialog.
   TestToneGenerator gen;
   gen.Configure(TestToneGenerator::Type::Sine, 1000.0, -10.0, 48000.0);
   std::vector<float> buf(2048);
   gen.Render(buf.data(), 1024);
   gen.Configure(TestToneGenerator::Type::Pink, 0.0, -10.0, 48000.0);
   gen.Render(buf.data() + 1024, 1024);
   for (float s : buf) {
      REQUIRE(std::isfinite(s));
      REQUIRE(std::fabs(s) <= 1.0f);
   }
}
