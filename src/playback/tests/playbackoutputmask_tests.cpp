/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include "au3-mixer/PlaybackOutputMask.h"

namespace {
TEST(PlaybackOutputMaskTest, EmptyMask)
{
    PlaybackOutputMask m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.lo, uint64_t(0));
    EXPECT_EQ(m.hi, uint64_t(0));
}

TEST(PlaybackOutputMaskTest, SingleBitLow)
{
    PlaybackOutputMask m;
    m.lo = uint64_t(1) << 5;
    EXPECT_FALSE(m.empty());
    EXPECT_TRUE(m.test(5));
    EXPECT_FALSE(m.test(4));
    EXPECT_FALSE(m.test(6));
    EXPECT_FALSE(m.test(64));
}

TEST(PlaybackOutputMaskTest, SingleBitHigh)
{
    PlaybackOutputMask m;
    m.hi = uint64_t(1) << 0;
    EXPECT_FALSE(m.empty());
    EXPECT_FALSE(m.test(63));
    EXPECT_TRUE(m.test(64));
    EXPECT_FALSE(m.test(65));
}

TEST(PlaybackOutputMaskTest, BoundaryBit63)
{
    // The boundary between lo and hi -- bit 63 lives in lo, bit 64 in hi.
    PlaybackOutputMask m;
    m.set(63);
    EXPECT_TRUE(m.test(63));
    EXPECT_NE(m.lo & (uint64_t(1) << 63), 0u);
    EXPECT_EQ(m.hi, uint64_t(0));
}

TEST(PlaybackOutputMaskTest, BoundaryBit64)
{
    PlaybackOutputMask m;
    m.set(64);
    EXPECT_TRUE(m.test(64));
    EXPECT_EQ(m.lo, uint64_t(0));
    EXPECT_EQ(m.hi & uint64_t(1), uint64_t(1));
}

TEST(PlaybackOutputMaskTest, IdentityHelper)
{
    auto m = PlaybackOutputMask::Identity(2, 4);
    // Bits 2, 3, 4, 5 set
    for (unsigned bit = 0; bit < 128; ++bit) {
        const bool expected = (bit >= 2 && bit < 6);
        EXPECT_EQ(m.test(bit), expected) << "bit " << bit;
    }
}

TEST(PlaybackOutputMaskTest, IdentityCrossesBoundary)
{
    // Identity that straddles the lo/hi boundary.
    auto m = PlaybackOutputMask::Identity(62, 4);
    EXPECT_TRUE(m.test(62));
    EXPECT_TRUE(m.test(63));
    EXPECT_TRUE(m.test(64));
    EXPECT_TRUE(m.test(65));
    EXPECT_FALSE(m.test(61));
    EXPECT_FALSE(m.test(66));
}

TEST(PlaybackOutputMaskTest, Equality)
{
    PlaybackOutputMask a;
    PlaybackOutputMask b;
    a.set(5);
    b.set(5);
    EXPECT_EQ(a, b);
    b.set(6);
    EXPECT_NE(a, b);
}

TEST(PlaybackOutputMaskTest, AtomicLoadStoreRoundTrip)
{
    AtomicPlaybackOutputMask atomic;
    PlaybackOutputMask m;
    m.set(7);
    m.set(70);
    atomic.Store(m);
    const auto loaded = atomic.Load();
    EXPECT_TRUE(loaded.test(7));
    EXPECT_TRUE(loaded.test(70));
    EXPECT_EQ(loaded, m);
}

TEST(PlaybackOutputMaskTest, MaskBitsCap)
{
    // Bit positions at the upper cap.
    PlaybackOutputMask m;
    m.set(127);
    EXPECT_TRUE(m.test(127));
    EXPECT_FALSE(m.test(128));   // out of range
    EXPECT_FALSE(m.test(126));
}
} // namespace
