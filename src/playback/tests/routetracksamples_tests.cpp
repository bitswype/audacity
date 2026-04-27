/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "au3-mixer/RouteTrackSamples.h"

namespace {
//! Test fixture providing source / master scratch buffers.
struct Routing {
    Routing(size_t numSourceChannels, size_t numOutputChannels, size_t samples)
        : srcStorage(numSourceChannels, std::vector<float>(samples, 0.0f))
        , masterStorage(numOutputChannels, std::vector<float>(samples, 0.0f))
        , srcPtrs(numSourceChannels)
        , masterPtrs(numOutputChannels)
    {
        for (size_t i = 0; i < numSourceChannels; ++i) {
            srcPtrs[i] = srcStorage[i].data();
        }
        for (size_t i = 0; i < numOutputChannels; ++i) {
            masterPtrs[i] = masterStorage[i].data();
        }
    }

    void fillSource(size_t ch, float v)
    {
        std::fill(srcStorage[ch].begin(), srcStorage[ch].end(), v);
    }

    float master(size_t ch, size_t i = 0) const
    {
        return masterStorage[ch][i];
    }

    std::vector<std::vector<float> > srcStorage;
    std::vector<std::vector<float> > masterStorage;
    std::vector<float*> srcPtrs;
    std::vector<float*> masterPtrs;
};

const auto kUnitVolume = [](int) { return 1.0f; };

TEST(RouteTrackSamplesTest, MonoSingleBitMask)
{
    Routing r(/*src*/ 1, /*out*/ 4, /*samples*/ 8);
    r.fillSource(0, 2.0f);
    TrackChannelAssignment a;
    a.outputMask.set(0);
    RouteTrackSamples(a, 1, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 2.0f);
    EXPECT_EQ(r.master(1), 0.0f);
    EXPECT_EQ(r.master(2), 0.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, MonoTwoBitsDuplicates)
{
    Routing r(1, 4, 8);
    r.fillSource(0, 3.0f);
    TrackChannelAssignment a;
    a.outputMask.set(0);
    a.outputMask.set(2);
    RouteTrackSamples(a, 1, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 3.0f);
    EXPECT_EQ(r.master(1), 0.0f);
    EXPECT_EQ(r.master(2), 3.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, StereoToContiguousBits)
{
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 4.0f);
    TrackChannelAssignment a;
    a.outputMask.set(0);
    a.outputMask.set(1);
    RouteTrackSamples(a, 2, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 1.0f);
    EXPECT_EQ(r.master(1), 4.0f);
    EXPECT_EQ(r.master(2), 0.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, StereoToNonContiguousBits)
{
    // 4-channel device, stereo source masked to outputs 2 and 3
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 4.0f);
    TrackChannelAssignment a;
    a.outputMask.set(2);
    a.outputMask.set(3);
    RouteTrackSamples(a, 2, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 0.0f);
    EXPECT_EQ(r.master(1), 0.0f);
    EXPECT_EQ(r.master(2), 1.0f);
    EXPECT_EQ(r.master(3), 4.0f);
}

TEST(RouteTrackSamplesTest, FourChannelSplitMaskFromPlanDoc)
{
    // From the design doc: a 4-channel clip with mask 0b1100_0011
    // distributes ch0..ch3 to outputs 0, 1, 6, 7.
    Routing r(4, 8, 8);
    r.fillSource(0, 10.0f);
    r.fillSource(1, 20.0f);
    r.fillSource(2, 30.0f);
    r.fillSource(3, 40.0f);
    TrackChannelAssignment a;
    a.outputMask.lo = 0b11000011;
    RouteTrackSamples(a, 4, 8, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 10.0f);
    EXPECT_EQ(r.master(1), 20.0f);
    EXPECT_EQ(r.master(2), 0.0f);
    EXPECT_EQ(r.master(3), 0.0f);
    EXPECT_EQ(r.master(4), 0.0f);
    EXPECT_EQ(r.master(5), 0.0f);
    EXPECT_EQ(r.master(6), 30.0f);
    EXPECT_EQ(r.master(7), 40.0f);
}

TEST(RouteTrackSamplesTest, MoreBitsThanSourcesDropsExtraBits)
{
    // Stereo source, three set bits: ch 0 -> bit 0, ch 1 -> bit 1,
    // bit 2 receives nothing (no remaining source).  Multi-channel
    // sources do not duplicate to fill leftover bits; that's a mono
    // source's behavior.
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 5.0f);
    TrackChannelAssignment a;
    a.outputMask.lo = 0b0111;
    RouteTrackSamples(a, 2, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 1.0f);
    EXPECT_EQ(r.master(1), 5.0f);
    EXPECT_EQ(r.master(2), 0.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, MonoSourceFillsAllBits)
{
    // Mono source replicates to every set bit -- the duplicate-to-fill
    // behavior is a property of mono input, not multi-channel.
    Routing r(1, 4, 8);
    r.fillSource(0, 7.0f);
    TrackChannelAssignment a;
    a.outputMask.lo = 0b0111;
    RouteTrackSamples(a, 1, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 7.0f);
    EXPECT_EQ(r.master(1), 7.0f);
    EXPECT_EQ(r.master(2), 7.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, FewerBitsThanSourcesDropsExtras)
{
    // Stereo source, one bit set -- only source channel 0 lands.
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 2.0f);
    TrackChannelAssignment a;
    a.outputMask.set(2);
    RouteTrackSamples(a, 2, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 0.0f);
    EXPECT_EQ(r.master(1), 0.0f);
    EXPECT_EQ(r.master(2), 1.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, HighWordBit64)
{
    // Mono source masked to bit 64 (in the hi word).
    Routing r(1, 128, 8);
    r.fillSource(0, 9.0f);
    TrackChannelAssignment a;
    a.outputMask.set(64);
    RouteTrackSamples(a, 1, 128, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(63), 0.0f);
    EXPECT_EQ(r.master(64), 9.0f);
    EXPECT_EQ(r.master(65), 0.0f);
}

TEST(RouteTrackSamplesTest, EmptyMaskNoOp)
{
    // Empty mask -- every output stays at zero.
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 2.0f);
    TrackChannelAssignment a; // empty mask
    RouteTrackSamples(a, 2, 4, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 0.0f);
    EXPECT_EQ(r.master(1), 0.0f);
    EXPECT_EQ(r.master(2), 0.0f);
    EXPECT_EQ(r.master(3), 0.0f);
}

TEST(RouteTrackSamplesTest, PerChannelVolumeApplied)
{
    Routing r(2, 4, 8);
    r.fillSource(0, 1.0f);
    r.fillSource(1, 1.0f);
    const auto vol = [](int c) { return c == 0 ? 0.5f : 2.0f; };
    TrackChannelAssignment a;
    a.outputMask.set(0);
    a.outputMask.set(1);
    RouteTrackSamples(a, 2, 4, 8, vol, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_NEAR(r.master(0), 0.5f, 1e-6f);
    EXPECT_NEAR(r.master(1), 2.0f, 1e-6f);
}

TEST(RouteTrackSamplesTest, MultipleCallsAccumulate)
{
    // Two tracks both routing to output 0 should sum.
    Routing r(1, 2, 8);
    r.fillSource(0, 1.0f);
    TrackChannelAssignment a;
    a.outputMask.set(0);
    RouteTrackSamples(a, 1, 2, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());

    // Reset source for track 2 and route again.
    r.fillSource(0, 3.0f);
    RouteTrackSamples(a, 1, 2, 8, kUnitVolume, r.srcPtrs.data(), r.masterPtrs.data());
    EXPECT_EQ(r.master(0), 4.0f);
    EXPECT_EQ(r.master(1), 0.0f);
}
} // namespace
