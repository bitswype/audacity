/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackInputMask.h

  Bitswype fork: 128-bit per-track recording-INPUT channel mask.
  Parallel structure to PlaybackOutputMask but applied in the
  recording direction.

  Semantics:
    - Empty mask (lo=0 && hi=0) means "this track is not a recording
      target".
    - Otherwise, bit N set (0 <= N < 128) means "device input channel N
      contributes to this track's recording".
    - For a mono target track (NChannels()==1), all set bits SUM into
      the single track channel.  This is the dual of playback's
      "mono source replicates to all set output bits".
    - For a multi-channel target track with N>=2 channels, the set
      bits walk in low-to-high order: bit 0 (lowest) -> track ch 0,
      next set bit -> track ch 1, etc.  Extra set bits beyond the
      track channel count are dropped.  Extra channels beyond the
      set bit count remain silent.  This is the dual of playback's
      multi-channel source rule.

  There is no global "no recording matrix" state -- if any project
  track has a non-empty input mask, recording uses matrix mode and
  the set of capture targets is determined by which tracks have
  non-empty masks.  Newly-created tracks have their identity bits
  filled in at creation time by the listener; legacy projects with
  no inputmask attribute get identity routing materialized at load
  time.

*******************************************************************/
#pragma once

#include "MixerOptions.h" // for MIXER_API

#include <atomic>
#include <cstdint>
#include <vector>

//! 128-bit per-track recording-input channel mask.
//!
//! Distinct type from PlaybackOutputMask so the compiler catches
//! direction mix-ups (passing an output mask where an input is
//! expected, or vice versa).  Bit-level semantics are identical.
struct MIXER_API PlaybackInputMask
{
   //! Bits 0..63.
   uint64_t lo = 0;
   //! Bits 64..127.
   uint64_t hi = 0;

   constexpr PlaybackInputMask() = default;
   constexpr PlaybackInputMask(uint64_t lo_, uint64_t hi_)
      : lo(lo_), hi(hi_) {}

   constexpr bool empty() const { return lo == 0 && hi == 0; }

   constexpr bool test(unsigned bit) const
   {
      return bit < 64
         ? ((lo >> bit) & uint64_t(1)) != 0
         : bit < 128
            ? ((hi >> (bit - 64)) & uint64_t(1)) != 0
            : false;
   }

   constexpr void set(unsigned bit)
   {
      if (bit < 64)
         lo |= uint64_t(1) << bit;
      else if (bit < 128)
         hi |= uint64_t(1) << (bit - 64);
   }

   constexpr void clear(unsigned bit)
   {
      if (bit < 64)
         lo &= ~(uint64_t(1) << bit);
      else if (bit < 128)
         hi &= ~(uint64_t(1) << (bit - 64));
   }

   //! Population count (number of set bits).  Note: this is NOT
   //! "highest bit + 1"; e.g. a mask with only bit 100 set returns 1.
   unsigned popcount() const;

   bool hasBitsAboveDeviceWidth(unsigned numDeviceChannels) const;

   //! Build an identity mask covering @p channelCount consecutive bits
   //! starting at @p firstChannel.  Bits past 127 are truncated.
   //! For recording: channels 0..N-1 of the track will read from
   //! device inputs firstChannel..firstChannel+N-1.
   static PlaybackInputMask Identity(
      unsigned firstChannel, unsigned channelCount);

   friend constexpr bool operator==(
      const PlaybackInputMask& a, const PlaybackInputMask& b)
   {
      return a.lo == b.lo && a.hi == b.hi;
   }
   friend constexpr bool operator!=(
      const PlaybackInputMask& a, const PlaybackInputMask& b)
   {
      return !(a == b);
   }
};

//! Maximum number of input channels representable by a
//! PlaybackInputMask.  Matches the output side; same underlying
//! storage shape.
constexpr unsigned kPlaybackInputMaskBits = 128;

//! Single-writer / multi-reader atomic accessor for PlaybackInputMask.
//! Uses a seqlock; see AtomicPlaybackOutputMask in
//! PlaybackOutputMask.h for the design rationale.
class MIXER_API AtomicPlaybackInputMask final
{
public:
   AtomicPlaybackInputMask() = default;

   AtomicPlaybackInputMask(const AtomicPlaybackInputMask& other) noexcept
   {
      Store(other.Load());
   }

   AtomicPlaybackInputMask& operator=(
      const AtomicPlaybackInputMask& other) noexcept
   {
      if (this != &other)
         Store(other.Load());
      return *this;
   }

   AtomicPlaybackInputMask(AtomicPlaybackInputMask&&) = delete;
   AtomicPlaybackInputMask& operator=(AtomicPlaybackInputMask&&) = delete;

   PlaybackInputMask Load() const noexcept;
   void Store(PlaybackInputMask mask) noexcept;

private:
   mutable std::atomic<uint64_t> mSeq{ 0 };
   std::atomic<uint64_t> mLo{ 0 };
   std::atomic<uint64_t> mHi{ 0 };
};

//! Choose how many columns the recording-routing matrix dialog
//! should show.  Same idea as ComputeRoutingDialogColumnCount on
//! the output side: include the device width, all set-bit positions
//! across track masks, and identity-Reset headroom for every row.
//! Capped at kPlaybackInputMaskBits.
MIXER_API
unsigned ComputeRecordingDialogColumnCount(
   unsigned deviceChannels,
   const std::vector<PlaybackInputMask>& trackMasks,
   const std::vector<unsigned>& trackChannelCounts);

//! Count tracks whose input mask has any bit set at index >=
//! @p deviceChannels.
MIXER_API
unsigned CountRecordingTracksWithBitsAboveDeviceWidth(
   unsigned deviceChannels,
   const std::vector<PlaybackInputMask>& trackMasks);
