/**********************************************************************

  Audacity: A Digital Audio Editor

  TestToneRender.cpp

  bitswype fork: see header.

**********************************************************************/
#include "TestToneRender.h"

#include "au3-mixer/ChannelRouting.h"     // TrackChannelAssignment
#include "au3-mixer/RouteTrackSamples.h"  // production routing engine

#include <algorithm>

bool RenderTestToneInterleaved(
    const TestToneRenderParams& params,
    TestToneGenerator& gen,
    std::size_t framesPerBuffer,
    float* outputInterleaved,
    std::vector<float>& srcScratch,
    std::vector<std::vector<float> >& outScratch,
    std::vector<float*>& dstScratch)
{
    if (!outputInterleaved
        || params.numPlaybackChannels == 0
        || params.mode == TestToneRequest::Mode::Off
        || params.mask.empty()
        || framesPerBuffer == 0) {
        return false;
    }

    // Tech-debt fall-back: grow the scratch if the caller under-
    // sized it.  Production callers in AudioIO pre-size in
    // StartTestTone so this branch is unreachable in practice;
    // tests may rely on the auto-grow.
    if (srcScratch.size() < framesPerBuffer) {
        srcScratch.resize(framesPerBuffer);
    }

    // 1. Synthesise the mono source.
    gen.Render(srcScratch.data(), framesPerBuffer);

    const std::size_t devCh = params.devicePlaybackChannels;
    const float* src = srcScratch.data();

    if (params.mode == TestToneRequest::Mode::DirectHW) {
        // Walk reachable set bits, write tone additively into the
        // interleaved output at that channel.  Bits past
        // numPlaybackChannels are ignored (device cannot reach them).
        for (unsigned ch = 0; ch < params.numPlaybackChannels; ++ch) {
            if (!params.mask.test(ch)) {
                continue;
            }
            for (std::size_t i = 0; i < framesPerBuffer; ++i) {
                outputInterleaved[devCh * i + ch] += src[i];
            }
        }
        return true;
    }

    // ThroughMatrix: route through the production engine, then
    // interleave back into the output buffer.
    if (outScratch.size() < params.numPlaybackChannels) {
        outScratch.resize(params.numPlaybackChannels);
    }
    for (auto& buf : outScratch) {
        if (buf.size() < framesPerBuffer) {
            buf.resize(framesPerBuffer);
        }
    }

    for (auto& buf : outScratch) {
        std::fill_n(buf.begin(), framesPerBuffer, 0.0f);
    }

    if (dstScratch.size() < params.numPlaybackChannels) {
        dstScratch.resize(params.numPlaybackChannels);
    }
    for (std::size_t n = 0; n < params.numPlaybackChannels; ++n) {
        dstScratch[n] = outScratch[n].data();
    }

    float* srcPtr = srcScratch.data();
    float* const* srcPtrs = &srcPtr;

    TrackChannelAssignment assignment;
    assignment.outputMask = params.mask;
    RouteTrackSamples(
        assignment,
        /*numSourceChannels*/ 1,
        /*numOutputChannels*/ params.numPlaybackChannels,
        /*samplesAvailable*/ framesPerBuffer,
        /*getChannelVolume*/ [](int) { return 1.0f; },
        srcPtrs,
        dstScratch.data());

    // Interleave the deinterleaved scratch back into the output.
    for (unsigned ch = 0; ch < params.numPlaybackChannels; ++ch) {
        const float* d = outScratch[ch].data();
        for (std::size_t i = 0; i < framesPerBuffer; ++i) {
            outputInterleaved[devCh * i + ch] += d[i];
        }
    }
    return true;
}
