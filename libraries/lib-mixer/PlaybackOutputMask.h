/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackOutputMask.h

  Bitswype fork: 128-bit per-track playback output channel mask.
  Replaces the earlier 64-bit uint64_t + sentinel-bit scheme.

  Semantics:
    - Empty mask (lo=0 && hi=0) means "this track is silent".
    - Otherwise, bit N set (0 <= N < 128) means "this track plays
      on device output channel N".
    - For multi-channel source tracks, source channels walk the set
      bits in low-to-high bit order (all of lo, then all of hi).
      Excess source channels (more sources than set bits) are dropped.

  There is no "auto routing" state -- every track carries an explicit
  mask.  Newly-created tracks have their identity bits filled in at
  creation time by the track list; legacy projects without an
  outputmask attribute get identity routing materialized at load time.

*******************************************************************/
#pragma once

#include "MixerOptions.h" // for MIXER_API

#include <cstdint>

//! 128-bit per-track playback output channel mask.
//!
//! See file-level doc comment for semantics.  This type is trivially
//! copyable and small (16 bytes) so it is passed by value.
struct MIXER_API PlaybackOutputMask
{
   //! Bits 0..63.
   uint64_t lo = 0;
   //! Bits 64..127.
   uint64_t hi = 0;

   constexpr PlaybackOutputMask() = default;
   constexpr PlaybackOutputMask(uint64_t lo_, uint64_t hi_)
      : lo(lo_), hi(hi_) {}

   //! True iff no bits are set (the "silent" state).
   constexpr bool empty() const { return lo == 0 && hi == 0; }

   //! True iff @p bit is set.  @p bit must be < 128.
   constexpr bool test(unsigned bit) const
   {
      return bit < 64
         ? ((lo >> bit) & uint64_t(1)) != 0
         : bit < 128
            ? ((hi >> (bit - 64)) & uint64_t(1)) != 0
            : false;
   }

   //! Set bit @p bit.  @p bit must be < 128; larger values are ignored.
   constexpr void set(unsigned bit)
   {
      if (bit < 64)
         lo |= uint64_t(1) << bit;
      else if (bit < 128)
         hi |= uint64_t(1) << (bit - 64);
   }

   //! Clear bit @p bit.  @p bit must be < 128; larger values are ignored.
   constexpr void clear(unsigned bit)
   {
      if (bit < 64)
         lo &= ~(uint64_t(1) << bit);
      else if (bit < 128)
         hi &= ~(uint64_t(1) << (bit - 64));
   }

   //! Highest set bit index + 1, or 0 if empty.
   //! Useful for sizing scans / dialog column counts.
   unsigned popcount() const;

   //! True iff any bit >= @p numDeviceChannels is set.
   //! Indicates the user has a routing for channels the current device
   //! cannot reach.
   bool hasBitsAboveDeviceWidth(unsigned numDeviceChannels) const;

   //! Build an identity mask covering @p channelCount consecutive bits
   //! starting at @p firstChannel.
   //! E.g. Identity(3, 2) sets bits 3 and 4.
   //! Bits past 127 are truncated.
   static PlaybackOutputMask Identity(
      unsigned firstChannel, unsigned channelCount);

   friend constexpr bool operator==(
      const PlaybackOutputMask& a, const PlaybackOutputMask& b)
   {
      return a.lo == b.lo && a.hi == b.hi;
   }
   friend constexpr bool operator!=(
      const PlaybackOutputMask& a, const PlaybackOutputMask& b)
   {
      return !(a == b);
   }
};

//! Maximum number of output channels representable by a PlaybackOutputMask.
constexpr unsigned kPlaybackOutputMaskBits = 128;
