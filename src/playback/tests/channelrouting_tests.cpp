/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include "au3-mixer/ChannelRouting.h"

namespace {
TEST(ChannelRoutingTest, EmptyInputProducesEmptyOutput)
{
    const auto out = ComputeChannelAssignments({});
    EXPECT_TRUE(out.empty());
}

TEST(ChannelRoutingTest, SingleEmptyMaskPassesThrough)
{
    std::vector<PlaybackOutputMask> masks(1);
    const auto out = ComputeChannelAssignments(masks);
    ASSERT_EQ(out.size(), size_t(1));
    EXPECT_TRUE(out[0].outputMask.empty());
}

TEST(ChannelRoutingTest, IdentityMaskPassesThrough)
{
    PlaybackOutputMask m;
    m.set(3);
    const auto out = ComputeChannelAssignments({ m });
    ASSERT_EQ(out.size(), size_t(1));
    EXPECT_TRUE(out[0].outputMask.test(3));
}

TEST(ChannelRoutingTest, MultipleTracksPreserveOrder)
{
    std::vector<PlaybackOutputMask> masks(3);
    masks[0].set(0);
    masks[1].set(1);
    masks[2].set(2);
    const auto out = ComputeChannelAssignments(masks);
    ASSERT_EQ(out.size(), size_t(3));
    EXPECT_TRUE(out[0].outputMask.test(0));
    EXPECT_TRUE(out[1].outputMask.test(1));
    EXPECT_TRUE(out[2].outputMask.test(2));
}

TEST(ChannelRoutingTest, HighBitMasksPassThroughVerbatim)
{
    PlaybackOutputMask hi;
    hi.set(70);
    const auto out = ComputeChannelAssignments({ hi });
    ASSERT_EQ(out.size(), size_t(1));
    EXPECT_TRUE(out[0].outputMask.test(70));
    EXPECT_FALSE(out[0].outputMask.test(6));   // not bit 6
}

TEST(ChannelRoutingTest, ManyOverlappingMasksUnchanged)
{
    // Two tracks with overlapping bits: ComputeChannelAssignments is a
    // pure pass-through and must NOT deduplicate.
    PlaybackOutputMask a, b;
    a.set(0);
    a.set(1);
    b.set(1);
    b.set(2);
    const auto out = ComputeChannelAssignments({ a, b });
    ASSERT_EQ(out.size(), size_t(2));
    EXPECT_TRUE(out[0].outputMask.test(0));
    EXPECT_TRUE(out[0].outputMask.test(1));
    EXPECT_TRUE(out[1].outputMask.test(1));
    EXPECT_TRUE(out[1].outputMask.test(2));
}
} // namespace
