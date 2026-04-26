/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneGenerator.cpp

  bitswype fork: see header.

**********************************************************************/
#include "TestToneGenerator.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

//! 10^(dB/20), with a hard floor at -200 dBFS == 1e-10.
double DbToLinear(double db)
{
   if (db <= -200.0)
      return 0.0;
   return std::pow(10.0, db / 20.0);
}

//! Number of trailing zeros in a 32-bit value (Voss-McCartney row index).
//! Returns 31 when @p x is 0 (matching the Audacity algorithm: the
//! "row" the lowest set bit lives in).  Uses __builtin_ctz where
//! available and a portable fallback elsewhere.
unsigned CountTrailingZeros32(uint32_t x)
{
   if (x == 0) return 31u;
#if defined(__GNUC__) || defined(__clang__)
   return static_cast<unsigned>(__builtin_ctz(x));
#elif defined(_MSC_VER)
   unsigned long index = 0;
   _BitScanForward(&index, x);
   return static_cast<unsigned>(index);
#else
   unsigned n = 0;
   while ((x & 1u) == 0u) { x >>= 1; ++n; }
   return n;
#endif
}
}

void TestToneGenerator::Configure(
   Type type, double frequencyHz, double levelDb, double sampleRate)
{
   mType = type;
   mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   mFrequencyHz = std::max(0.0, frequencyHz);
   mLinearAmp = DbToLinear(levelDb);
   mPhaseInc = kTwoPi * mFrequencyHz / mSampleRate;
}

void TestToneGenerator::Reset()
{
   mPhase = 0.0;
   for (auto& r : mPinkRows) r = 0;
   mPinkRunningSum = 0;
   mPinkCount = 0;
   mRngState = 0x9E3779B97F4A7C15ull;
}

bool TestToneGenerator::ExceedsNyquist() const
{
   // Use >= so a sine at exactly fs/2 also short-circuits to
   // silence.  At fs/2 a phase-accumulator sine emits sin(phi),
   // sin(phi+pi), sin(phi+2pi), ... -- a degenerate two-sample
   // alternation no DAC can reproduce faithfully and that the
   // user almost certainly didn't want.  The strict > variant
   // also let the test "Sine matches its configured frequency"
   // miss this edge case for any test that happened to land on
   // Nyquist exactly.
   return mType == Type::Sine && mFrequencyHz >= mSampleRate * 0.5;
}

float TestToneGenerator::NextSineSample()
{
   const double s = std::sin(mPhase) * mLinearAmp;
   mPhase += mPhaseInc;
   if (mPhase >= kTwoPi)
      mPhase -= kTwoPi;
   return static_cast<float>(s);
}

float TestToneGenerator::NextWhiteSample()
{
   // xorshift64 -- one of Marsaglia's; full 2^64-1 period.
   uint64_t x = mRngState;
   x ^= x << 13;
   x ^= x >> 7;
   x ^= x << 17;
   mRngState = x;
   // Map the top 32 bits to [-1, +1].
   const uint32_t bits = static_cast<uint32_t>(x >> 32);
   const double scaled = (static_cast<double>(bits) / 4294967295.0) * 2.0 - 1.0;
   return static_cast<float>(scaled * mLinearAmp);
}

float TestToneGenerator::NextPinkSample()
{
   // Voss-McCartney with 7 rows: replace the row indexed by the
   // count of trailing zeros in mPinkCount; sum all rows.  Each row
   // changes at half the rate of the previous, giving the 1/f
   // spectrum.  The output is normalized to roughly [-1, +1] before
   // applying the level.
   ++mPinkCount;
   constexpr unsigned kRows = 7u;
   const unsigned row = CountTrailingZeros32(mPinkCount);
   if (row < kRows) {
      // New row sample: take next white-noise bits scaled into a
      // signed 16-bit range so the sum stays bounded.  We bypass
      // NextWhiteSample to keep the integer pipeline.
      uint64_t x = mRngState;
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      mRngState = x;
      const uint32_t newRow = static_cast<uint32_t>(x >> 32) >> 16; // 16 bits
      mPinkRunningSum -= mPinkRows[row];
      mPinkRunningSum += newRow;
      mPinkRows[row] = newRow;
   }
   // Worst-case sum bound: kRows * 2^16 = 7 * 65536 = 458752.  Use
   // half-range as the divisor so output stays well within [-1,1].
   constexpr double kPinkScale = 1.0 / (3.5 * 65536.0);
   const double centered = static_cast<double>(mPinkRunningSum)
      * kPinkScale - 1.0;
   return static_cast<float>(centered * mLinearAmp);
}

void TestToneGenerator::Render(float* dest, std::size_t numFrames)
{
   if (mLinearAmp == 0.0 || ExceedsNyquist()) {
      std::fill(dest, dest + numFrames, 0.0f);
      return;
   }
   switch (mType) {
   case Type::Sine:
      for (std::size_t i = 0; i < numFrames; ++i)
         dest[i] = NextSineSample();
      break;
   case Type::Pink:
      for (std::size_t i = 0; i < numFrames; ++i)
         dest[i] = NextPinkSample();
      break;
   case Type::White:
      for (std::size_t i = 0; i < numFrames; ++i)
         dest[i] = NextWhiteSample();
      break;
   }
}
