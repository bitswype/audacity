/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  FakePlayableSequence.h

  A test double for PlayableSequence that returns deterministic audio
  data. Unlike MockPlayableSequence (which returns empty buffers),
  this fills DoGet() with configurable per-channel sample data.

**********************************************************************/
#pragma once

#include "au3-mixer/AudioIOSequences.h"
#include "au3-audio-graph/AudioGraphChannel.h"
#include "au3-math/SampleFormat.h"
#include "au3-math/SampleCount.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <cassert>

class FakePlayableSequence final : public PlayableSequence
{
public:
   FakePlayableSequence(
      size_t numChannels,
      double sampleRate,
      std::vector<std::vector<float>> channelData)
      : mNumChannels(numChannels)
      , mSampleRate(sampleRate)
      , mChannelData(std::move(channelData))
   {
      assert(mChannelData.size() == mNumChannels);
   }

   // Factory: create a sequence with a DC offset per channel
   static FakePlayableSequence DC(
      size_t numChannels, double sampleRate, size_t numSamples,
      const std::vector<float>& dcValues)
   {
      assert(dcValues.size() == numChannels);
      std::vector<std::vector<float>> data(numChannels);
      for (size_t ch = 0; ch < numChannels; ++ch)
         data[ch].assign(numSamples, dcValues[ch]);
      return FakePlayableSequence(numChannels, sampleRate, std::move(data));
   }

   // Factory: create a sequence with unique sine per channel
   static FakePlayableSequence Sine(
      size_t numChannels, double sampleRate, size_t numSamples,
      const std::vector<double>& frequencies, float amplitude = 0.5f)
   {
      assert(frequencies.size() == numChannels);
      std::vector<std::vector<float>> data(numChannels);
      for (size_t ch = 0; ch < numChannels; ++ch) {
         data[ch].resize(numSamples);
         for (size_t i = 0; i < numSamples; ++i)
            data[ch][i] = amplitude *
               static_cast<float>(std::sin(
                  2.0 * M_PI * frequencies[ch] * i / sampleRate));
      }
      return FakePlayableSequence(numChannels, sampleRate, std::move(data));
   }

   // WideSampleSequence
   bool DoGet(
      size_t iChannel, size_t nBuffers, const samplePtr buffers[],
      sampleFormat format, sampleCount start, size_t len, bool backwards,
      fillFormat fill = FillFormat::fillZero, bool mayThrow = true,
      sampleCount* pNumWithinClips = nullptr) const override
   {
      assert(iChannel + nBuffers <= mNumChannels);
      assert(format == floatSample); // Only float supported in tests

      for (size_t buf = 0; buf < nBuffers; ++buf) {
         auto* dst = reinterpret_cast<float*>(buffers[buf]);
         const auto& src = mChannelData[iChannel + buf];
         const auto startIdx = start.as_size_t();
         const auto count = std::min(len, src.size() - startIdx);

         // Copy available samples
         if (count > 0)
            std::memcpy(dst, src.data() + startIdx, count * sizeof(float));

         // Zero-fill remainder
         if (count < len)
            std::memset(dst + count, 0, (len - count) * sizeof(float));
      }

      if (pNumWithinClips)
         *pNumWithinClips = len;

      return true;
   }

   size_t NChannels() const override { return mNumChannels; }

   float GetChannelVolume(int channel) const override
   {
      if (channel < static_cast<int>(mChannelVolumes.size()))
         return mChannelVolumes[channel];
      // Default: channel % 2 pan model (matches WaveTrack behavior)
      float left = 1.0f, right = 1.0f;
      if (mPan < 0) right = mPan + 1.0f;
      else if (mPan > 0) left = 1.0f - mPan;
      return ((channel % 2) == 0) ? left * mVolume : right * mVolume;
   }

   double GetStartTime() const override { return 0.0; }
   double GetEndTime() const override
   {
      return mChannelData.empty() ? 0.0 :
         static_cast<double>(mChannelData[0].size()) / mSampleRate;
   }
   double GetRate() const override { return mSampleRate; }
   sampleFormat WidestEffectiveFormat() const override { return floatSample; }
   bool HasTrivialEnvelope() const override { return true; }
   void GetEnvelopeValues(
      double* buffer, size_t bufferLen, double t0,
      bool backwards) const override
   {
      std::fill(buffer, buffer + bufferLen, 1.0);
   }

   // AudioGraph::Channel
   AudioGraph::ChannelType GetChannelType() const override
   {
      if (mNumChannels == 1)
         return AudioGraph::MonoChannel;
      // For multi-channel, return based on channel index
      // (only called for single-channel query context)
      return AudioGraph::LeftChannel;
   }

   // PlayableSequence
   const ChannelGroup* FindChannelGroup() const override { return nullptr; }
   bool GetSolo() const override { return mSolo; }
   bool GetMute() const override { return mMute; }

   // Test configuration setters
   void SetVolume(float volume) { mVolume = volume; }
   void SetPan(float pan) { mPan = pan; }
   void SetSolo(bool solo) { mSolo = solo; }
   void SetMute(bool mute) { mMute = mute; }
   void SetChannelVolumes(std::vector<float> volumes)
   {
      mChannelVolumes = std::move(volumes);
   }

private:
   const size_t mNumChannels;
   const double mSampleRate;
   const std::vector<std::vector<float>> mChannelData;
   float mVolume = 1.0f;
   float mPan = 0.0f;
   bool mSolo = false;
   bool mMute = false;
   std::vector<float> mChannelVolumes;
};
