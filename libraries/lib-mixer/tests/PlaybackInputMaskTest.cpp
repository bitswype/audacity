/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackInputMaskTest.cpp

  Tests for PlaybackInputMask: bit ops across the 64-bit word
  boundary, Identity, popcount, hasBitsAboveDeviceWidth, equality,
  the dialog column-count helper, and the off-device tracks count.

  These cases mirror PlaybackOutputMaskTest.cpp; the two types share
  bit-level semantics by design but are distinct so the compiler
  catches direction mix-ups.

**********************************************************************/
#include "PlaybackInputMask.h"

#include <catch2/catch.hpp>

TEST_CASE("PlaybackInputMask: default is empty",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   CHECK(m.empty());
   CHECK(m.lo == 0);
   CHECK(m.hi == 0);
   CHECK(m.popcount() == 0u);
   for (unsigned b = 0; b < 128; ++b)
      CHECK_FALSE(m.test(b));
}

TEST_CASE("PlaybackInputMask: set/test/clear in low word",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(0);
   m.set(5);
   m.set(63);
   CHECK(m.test(0));
   CHECK(m.test(5));
   CHECK(m.test(63));
   CHECK_FALSE(m.test(1));
   CHECK_FALSE(m.test(64));
   CHECK(m.hi == 0);
   CHECK_FALSE(m.empty());
   CHECK(m.popcount() == 3u);

   m.clear(5);
   CHECK_FALSE(m.test(5));
   CHECK(m.popcount() == 2u);
}

TEST_CASE("PlaybackInputMask: set/test/clear in high word",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(64);
   m.set(100);
   m.set(127);
   CHECK(m.test(64));
   CHECK(m.test(100));
   CHECK(m.test(127));
   CHECK_FALSE(m.test(0));
   CHECK_FALSE(m.test(63));
   CHECK_FALSE(m.test(65));
   CHECK(m.lo == 0);
   CHECK_FALSE(m.empty());
   CHECK(m.popcount() == 3u);

   m.clear(100);
   CHECK_FALSE(m.test(100));
   CHECK(m.popcount() == 2u);
}

TEST_CASE("PlaybackInputMask: boundary bit 63 -> 64",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(63);
   m.set(64);
   CHECK(m.test(63));
   CHECK(m.test(64));
   CHECK(m.lo == (uint64_t(1) << 63));
   CHECK(m.hi == uint64_t(1));
   CHECK(m.popcount() == 2u);
}

TEST_CASE("PlaybackInputMask: bits past 127 silently ignored",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(128);
   m.set(1000);
   CHECK(m.empty());
   CHECK_FALSE(m.test(128));
   CHECK_FALSE(m.test(1000));
}

TEST_CASE("PlaybackInputMask: Identity helper",
   "[PlaybackInputMask]")
{
   SECTION("mono at channel 0")
   {
      const auto m = PlaybackInputMask::Identity(0, 1);
      CHECK(m.lo == uint64_t(1));
      CHECK(m.hi == 0);
   }

   SECTION("stereo at channel 3")
   {
      const auto m = PlaybackInputMask::Identity(3, 2);
      CHECK(m.test(3));
      CHECK(m.test(4));
      CHECK(m.popcount() == 2u);
   }

   SECTION("spans the 64-bit boundary")
   {
      const auto m = PlaybackInputMask::Identity(62, 4);
      CHECK(m.test(62));
      CHECK(m.test(63));
      CHECK(m.test(64));
      CHECK(m.test(65));
      CHECK(m.popcount() == 4u);
   }

   SECTION("truncated at 128")
   {
      const auto m = PlaybackInputMask::Identity(126, 5);
      CHECK(m.test(126));
      CHECK(m.test(127));
      CHECK_FALSE(m.test(128));
      CHECK(m.popcount() == 2u);
   }

   SECTION("empty channel count returns empty mask")
   {
      const auto m = PlaybackInputMask::Identity(10, 0);
      CHECK(m.empty());
   }
}

TEST_CASE("PlaybackInputMask: hasBitsAboveDeviceWidth",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(3);
   CHECK_FALSE(m.hasBitsAboveDeviceWidth(4));
   CHECK_FALSE(m.hasBitsAboveDeviceWidth(8));
   CHECK(m.hasBitsAboveDeviceWidth(3));
   CHECK(m.hasBitsAboveDeviceWidth(2));

   m.set(70);
   CHECK(m.hasBitsAboveDeviceWidth(8));
   CHECK(m.hasBitsAboveDeviceWidth(64));
   CHECK(m.hasBitsAboveDeviceWidth(70));
   CHECK_FALSE(m.hasBitsAboveDeviceWidth(71));
   CHECK_FALSE(m.hasBitsAboveDeviceWidth(128));
}

TEST_CASE("PlaybackInputMask: equality", "[PlaybackInputMask]")
{
   PlaybackInputMask a;
   PlaybackInputMask b;
   CHECK(a == b);
   a.set(10);
   CHECK(a != b);
   b.set(10);
   CHECK(a == b);
   a.set(100);
   CHECK(a != b);
}

TEST_CASE("ComputeRecordingDialogColumnCount: empty inputs",
   "[PlaybackInputMask]")
{
   CHECK(ComputeRecordingDialogColumnCount(2, {}, {}) == 2u);
   CHECK(ComputeRecordingDialogColumnCount(0, {}, {}) == 2u);
   CHECK(ComputeRecordingDialogColumnCount(8, {}, {}) == 8u);
}

TEST_CASE(
   "ComputeRecordingDialogColumnCount: device dominates with small masks",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(0);
   CHECK(ComputeRecordingDialogColumnCount(8, { m }, { 1 }) == 8u);
}

TEST_CASE(
   "ComputeRecordingDialogColumnCount: max set bit dominates above device",
   "[PlaybackInputMask]")
{
   PlaybackInputMask m;
   m.set(15);
   CHECK(ComputeRecordingDialogColumnCount(2, { m }, { 1 }) == 16u);

   PlaybackInputMask hi;
   hi.set(70);
   CHECK(ComputeRecordingDialogColumnCount(2, { hi }, { 1 }) == 71u);
}

TEST_CASE(
   "ComputeRecordingDialogColumnCount: identity headroom for every row",
   "[PlaybackInputMask]")
{
   std::vector<PlaybackInputMask> masks(10);
   std::vector<unsigned> chans(10, 1);
   CHECK(ComputeRecordingDialogColumnCount(2, masks, chans) == 10u);

   chans = { 1, 1, 1, 1, 1, 2 };
   masks.clear();
   masks.resize(chans.size());
   CHECK(ComputeRecordingDialogColumnCount(2, masks, chans) == 7u);
}

TEST_CASE(
   "ComputeRecordingDialogColumnCount: capped at kPlaybackInputMaskBits",
   "[PlaybackInputMask]")
{
   CHECK(
      ComputeRecordingDialogColumnCount(1000, {}, {}) ==
      kPlaybackInputMaskBits);

   PlaybackInputMask m;
   m.set(127);
   CHECK(
      ComputeRecordingDialogColumnCount(2, { m }, { 1 }) ==
      kPlaybackInputMaskBits);
}

TEST_CASE("CountRecordingTracksWithBitsAboveDeviceWidth: counts",
   "[PlaybackInputMask]")
{
   PlaybackInputMask a, b, c;
   a.set(0); a.set(1);
   b.set(10);
   c.set(70);
   CHECK(CountRecordingTracksWithBitsAboveDeviceWidth(2, { a, b, c }) == 2u);
   CHECK(CountRecordingTracksWithBitsAboveDeviceWidth(11, { a, b, c }) == 1u);
   CHECK(CountRecordingTracksWithBitsAboveDeviceWidth(71, { a, b, c }) == 0u);
   CHECK(CountRecordingTracksWithBitsAboveDeviceWidth(2, {}) == 0u);
}
