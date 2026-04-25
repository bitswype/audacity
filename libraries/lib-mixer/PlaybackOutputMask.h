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

#include <atomic>
#include <cstdint>
#include <vector>

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

   //! Population count: number of bits set in the mask.
   //! NOT to be confused with the highest-set-bit index, which is a
   //! different quantity (e.g. {bit 100 set} has popcount 1 and
   //! highest-set-bit 100).
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

//! Choose how many columns the routing matrix dialog should show.
//!
//! Returns the smallest column count that simultaneously:
//!   - Includes every device output channel (so identity-routing is
//!     always editable for the current device).
//!   - Includes every bit set in any of @p trackMasks (so off-device
//!     bits stored on tracks remain visible / clearable instead of
//!     silently dropping out of the user's reach).
//!   - Has room for an identity-Reset of every row (each row r with
//!     channel count c needs at least r + c columns).
//!
//! Capped at kPlaybackOutputMaskBits.  Always at least 2 to avoid an
//! awkward 1-column dialog on a mono device with no tracks.
//!
//! @p trackChannelCounts must have the same size as @p trackMasks.
MIXER_API
unsigned ComputeRoutingDialogColumnCount(
   unsigned deviceChannels,
   const std::vector<PlaybackOutputMask>& trackMasks,
   const std::vector<unsigned>& trackChannelCounts);

//! Count tracks whose mask has any bit set at index >= @p deviceChannels.
//! Used to surface a project-load notice when the user opens a project
//! whose routing references channels their current device cannot reach.
MIXER_API
unsigned CountTracksWithBitsAboveDeviceWidth(
   unsigned deviceChannels,
   const std::vector<PlaybackOutputMask>& trackMasks);

//! Single-writer / multi-reader atomic accessor for PlaybackOutputMask.
//!
//! The 128-bit mask is split across two 64-bit atomic words because
//! std::atomic<__int128_t> is not portably lock-free.  A naive
//! "store lo, store hi" / "load lo, load hi" pair leaves a window in
//! which a reader can observe lo from one Store call paired with hi
//! from a different Store call -- a torn read.
//!
//! This class closes that window with a seqlock (Linux-kernel style):
//! Store increments a sequence counter to an odd value, performs the
//! lo/hi stores, then increments to even.  Load reads the counter,
//! reads lo/hi, re-reads the counter; if either snapshot saw an odd
//! value or the two reads disagree, Load retries.  The retry path is
//! finite-step under any single-writer schedule because the writer is
//! always making forward progress on the counter.
//!
//! Constraints:
//!   - At most one writer thread.  Concurrent writers can both observe
//!     the same "current seq" and stamp interleaved stores.  In
//!     practice the only writers are the GUI thread (dialog Apply,
//!     PlaybackRoutingListener); they are serialized.
//!   - Any number of reader threads, including the audio worker.
//!   - Reads are wait-free under no-contention; under contention they
//!     spin until the writer finishes (microseconds).
class MIXER_API AtomicPlaybackOutputMask final
{
public:
   AtomicPlaybackOutputMask() = default;

   //! Copy-construct by snapshotting the source.  Required because
   //! std::atomic is non-copyable, but WaveTrackData::Clone copies the
   //! enclosing struct.
   AtomicPlaybackOutputMask(const AtomicPlaybackOutputMask& other) noexcept
   {
      Store(other.Load());
   }

   AtomicPlaybackOutputMask& operator=(
      const AtomicPlaybackOutputMask& other) noexcept
   {
      if (this != &other)
         Store(other.Load());
      return *this;
   }

   AtomicPlaybackOutputMask(AtomicPlaybackOutputMask&&) = delete;
   AtomicPlaybackOutputMask& operator=(AtomicPlaybackOutputMask&&) = delete;

   //! Read the current mask value.  Safe to call from any thread.
   //! Will retry under contention with a writer.
   PlaybackOutputMask Load() const noexcept;

   //! Write a new mask value.  Single-writer only.
   void Store(PlaybackOutputMask mask) noexcept;

private:
   //! Even when stable; odd when a writer is mid-update.
   //! Mutable so Load() (a const operation) can read it without
   //! casts at every call site.
   mutable std::atomic<uint64_t> mSeq{ 0 };
   std::atomic<uint64_t> mLo{ 0 };
   std::atomic<uint64_t> mHi{ 0 };
};
