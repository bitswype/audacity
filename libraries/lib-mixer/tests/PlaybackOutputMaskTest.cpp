/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackOutputMaskTest.cpp

  Unit tests for the 128-bit PlaybackOutputMask struct: set/test/clear
  across the lo/hi word boundary, Identity construction, empty,
  popcount, and device-width checks.

**********************************************************************/
#include "PlaybackOutputMask.h"

#include <catch2/catch.hpp>

TEST_CASE("PlaybackOutputMask: default is empty", "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
   CHECK(m.empty());
   CHECK(m.lo == 0);
   CHECK(m.hi == 0);
   CHECK(m.popcount() == 0u);
   for (unsigned b = 0; b < 128; ++b)
      CHECK_FALSE(m.test(b));
}

TEST_CASE("PlaybackOutputMask: set/test/clear in low word",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
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

TEST_CASE("PlaybackOutputMask: set/test/clear in high word",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
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

TEST_CASE("PlaybackOutputMask: boundary bit 63 -> 64",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
   m.set(63);
   m.set(64);
   CHECK(m.test(63));
   CHECK(m.test(64));
   CHECK(m.lo == (uint64_t(1) << 63));
   CHECK(m.hi == uint64_t(1));
   CHECK(m.popcount() == 2u);
}

TEST_CASE("PlaybackOutputMask: bits past 127 silently ignored",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
   m.set(128);
   m.set(1000);
   CHECK(m.empty());
   CHECK_FALSE(m.test(128));
   CHECK_FALSE(m.test(1000));
}

TEST_CASE("PlaybackOutputMask: Identity helper", "[PlaybackOutputMask]")
{
   SECTION("mono at channel 0")
   {
      const auto m = PlaybackOutputMask::Identity(0, 1);
      CHECK(m.lo == uint64_t(1));
      CHECK(m.hi == 0);
   }

   SECTION("stereo at channel 3")
   {
      const auto m = PlaybackOutputMask::Identity(3, 2);
      CHECK(m.test(3));
      CHECK(m.test(4));
      CHECK(m.popcount() == 2u);
   }

   SECTION("spans the 64-bit boundary")
   {
      const auto m = PlaybackOutputMask::Identity(62, 4);
      CHECK(m.test(62));
      CHECK(m.test(63));
      CHECK(m.test(64));
      CHECK(m.test(65));
      CHECK(m.popcount() == 4u);
   }

   SECTION("truncated at 128")
   {
      const auto m = PlaybackOutputMask::Identity(126, 5);
      CHECK(m.test(126));
      CHECK(m.test(127));
      CHECK_FALSE(m.test(128));
      CHECK(m.popcount() == 2u);
   }

   SECTION("empty channel count returns empty mask")
   {
      const auto m = PlaybackOutputMask::Identity(10, 0);
      CHECK(m.empty());
   }
}

TEST_CASE("PlaybackOutputMask: hasBitsAboveDeviceWidth",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
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

TEST_CASE("ComputeRoutingDialogColumnCount: empty inputs",
   "[PlaybackOutputMask]")
{
   // No tracks: column count is just max(2, device).
   CHECK(ComputeRoutingDialogColumnCount(2, {}, {}) == 2u);
   CHECK(ComputeRoutingDialogColumnCount(0, {}, {}) == 2u);
   CHECK(ComputeRoutingDialogColumnCount(1, {}, {}) == 2u);
   CHECK(ComputeRoutingDialogColumnCount(8, {}, {}) == 8u);
}

TEST_CASE(
   "ComputeRoutingDialogColumnCount: device dominates when masks are small",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
   m.set(0);
   m.set(1);
   CHECK(ComputeRoutingDialogColumnCount(8, { m }, { 1 }) == 8u);
}

TEST_CASE(
   "ComputeRoutingDialogColumnCount: max set bit dominates when above device",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask m;
   m.set(15);
   // Device has 2 channels, mask references channel 15 -> need 16 cols.
   CHECK(ComputeRoutingDialogColumnCount(2, { m }, { 1 }) == 16u);

   PlaybackOutputMask hi;
   hi.set(70);
   CHECK(ComputeRoutingDialogColumnCount(2, { hi }, { 1 }) == 71u);
   CHECK(ComputeRoutingDialogColumnCount(2, { hi }, { 1 }) > 64u);
}

TEST_CASE(
   "ComputeRoutingDialogColumnCount: identity headroom for every row",
   "[PlaybackOutputMask]")
{
   // 10 mono tracks with empty masks need at least 10 columns so that
   // Reset on row 9 can assign identity at bit 9.
   std::vector<PlaybackOutputMask> masks(10);
   std::vector<unsigned> chans(10, 1);
   CHECK(ComputeRoutingDialogColumnCount(2, masks, chans) == 10u);

   // Row 5 with stereo channel count needs through bit 6.
   masks.clear();
   chans = { 1, 1, 1, 1, 1, 2 };
   masks.resize(chans.size());
   CHECK(ComputeRoutingDialogColumnCount(2, masks, chans) == 7u);
}

TEST_CASE(
   "ComputeRoutingDialogColumnCount: capped at kPlaybackOutputMaskBits",
   "[PlaybackOutputMask]")
{
   // Even if device > 128 (which the dialog itself caps), the helper
   // should not allow a column count beyond the mask width.
   CHECK(
      ComputeRoutingDialogColumnCount(1000, {}, {}) ==
      kPlaybackOutputMaskBits);

   PlaybackOutputMask m;
   m.set(127);
   CHECK(
      ComputeRoutingDialogColumnCount(2, { m }, { 1 }) ==
      kPlaybackOutputMaskBits);

   // Identity headroom request that would exceed the cap clamps too.
   std::vector<PlaybackOutputMask> masks(200);
   std::vector<unsigned> chans(200, 1);
   CHECK(
      ComputeRoutingDialogColumnCount(2, masks, chans) ==
      kPlaybackOutputMaskBits);
}

TEST_CASE(
   "ComputeRoutingDialogColumnCount: zero-channel rows are skipped",
   "[PlaybackOutputMask]")
{
   // A track with NChannels==0 contributes nothing to the identity
   // headroom (bug guard: it must not push the count to an absurd
   // value via row+0).
   std::vector<PlaybackOutputMask> masks(5);
   std::vector<unsigned> chans = { 1, 1, 0, 1, 1 };
   CHECK(ComputeRoutingDialogColumnCount(2, masks, chans) == 5u);
}

TEST_CASE("CountTracksWithBitsAboveDeviceWidth: basic counts",
   "[PlaybackOutputMask]")
{
   PlaybackOutputMask a, b, c;
   a.set(0); a.set(1);   // within 2-channel device
   b.set(10);             // beyond 2-channel device
   c.set(70);             // beyond 64-channel device
   CHECK(CountTracksWithBitsAboveDeviceWidth(2, { a, b, c }) == 2u);
   CHECK(CountTracksWithBitsAboveDeviceWidth(11, { a, b, c }) == 1u);
   CHECK(CountTracksWithBitsAboveDeviceWidth(71, { a, b, c }) == 0u);
   CHECK(CountTracksWithBitsAboveDeviceWidth(2, {}) == 0u);
}

TEST_CASE("PlaybackOutputMask: equality", "[PlaybackOutputMask]")
{
   PlaybackOutputMask a;
   PlaybackOutputMask b;
   CHECK(a == b);
   a.set(10);
   CHECK(a != b);
   b.set(10);
   CHECK(a == b);
   a.set(100);
   CHECK(a != b);
}
