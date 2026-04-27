/**********************************************************************

  Audacity: A Digital Audio Editor

  RouteRecordingSamples.cpp

*******************************************************************/
#include "RouteRecordingSamples.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
   #define AU_CTZ64(x) static_cast<unsigned>(__builtin_ctzll(x))
#else
   #include <intrin.h>
static inline unsigned AU_CTZ64(uint64_t x)
{
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return static_cast<unsigned>(idx);
}

#endif

namespace {
//! Visit set bits of @p mask in low-to-high bit order, stopping at
//! bits past @p numDeviceChannels.  Mirrors ForEachSetBitInRange in
//! RouteTrackSamples.cpp.
template<typename F>
void ForEachInRange(
    const PlaybackInputMask& mask, size_t numDeviceChannels, F&& visit)
{
    const unsigned cap = static_cast<unsigned>(
        std::min<size_t>(numDeviceChannels, kPlaybackInputMaskBits));
    uint64_t w = mask.lo;
    while (w != 0) {
        const unsigned b = AU_CTZ64(w);
        if (b >= cap) {
            return;
        }
        visit(b);
        w &= w - 1;
    }
    w = mask.hi;
    while (w != 0) {
        const unsigned b = 64u + AU_CTZ64(w);
        if (b >= cap) {
            return;
        }
        visit(b);
        w &= w - 1;
    }
}
} // namespace

void RouteRecordingSamples(
    const PlaybackInputMask& mask,
    size_t numTrackChannels,
    size_t trackChannel,
    size_t numDeviceChannels,
    size_t samplesAvailable,
    const float* const* stagingBuffers,
    float* destBuffer)
{
    // Always zero the destination first so that "no contribution"
    // paths leave silence (rather than uninitialised memory).
    std::memset(destBuffer, 0, samplesAvailable * sizeof(float));

    if (mask.empty() || numTrackChannels == 0) {
        return;
    }

    if (numTrackChannels == 1) {
        // Mono target: SUM every in-range set bit into the destination.
        ForEachInRange(mask, numDeviceChannels,
                       [&](unsigned bit) {
            const float* src = stagingBuffers[bit];
            for (size_t i = 0; i < samplesAvailable; ++i) {
                destBuffer[i] += src[i];
            }
        });
        return;
    }

    // Multi target: take the trackChannel-th set bit (low-to-high).
    // If popcount < numTrackChannels and trackChannel falls beyond,
    // the destination stays silent (zeroed above).  Extras beyond
    // numTrackChannels are not our concern -- callers pass
    // trackChannel < numTrackChannels.
    size_t skipsRemaining = trackChannel;
    bool wrote = false;
    ForEachInRange(mask, numDeviceChannels,
                   [&](unsigned bit) {
        if (wrote) {
            return;
        }
        if (skipsRemaining > 0) {
            --skipsRemaining;
            return;
        }
        const float* src = stagingBuffers[bit];
        std::memcpy(destBuffer, src, samplesAvailable * sizeof(float));
        wrote = true;
    });
}
