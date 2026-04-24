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
