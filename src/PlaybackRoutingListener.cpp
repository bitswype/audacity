/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingListener.cpp

  Bitswype fork: materializes identity playback routing masks for
  WaveTracks that don't already have one.  Runs at two points:

    1. Project-attachment construction: walks existing tracks and
       assigns identity routing to any WaveTrack that was loaded from
       a project without outputmask* attrs (i.e. upstream or
       pre-feature projects).

    2. On every TrackList::ADDITION event: if the added track is a
       WaveTrack with an empty mask and no attr was seen (i.e.
       created in-app or just imported), assigns identity routing at
       the next-free channel slot.

  Identity routing is "track N's channels -> consecutive output bits
  starting at N".  Positions are computed by walking the current track
  list and choosing the lowest free range wide enough for the new
  track's channel count.

  See .claude/plans/playback-routing-128bit-static-masks.md.

**********************************************************************/

#include "ClientData.h"
#include "Observer.h"
#include "PlaybackInputMask.h"
#include "PlaybackOutputMask.h"
#include "Project.h"
#include "Track.h"
#include "WaveTrack.h"

#include <memory>

namespace
{
//! Union of all set bits across WaveTracks in @p list, ignoring
//! @p exclude (e.g. the track just being added).
PlaybackOutputMask OccupiedBits(
   const TrackList& list, const Track* exclude = nullptr)
{
   PlaybackOutputMask occupied;
   for (const auto& track : list) {
      if (track == exclude)
         continue;
      const auto* wt = dynamic_cast<const WaveTrack*>(track);
      if (!wt)
         continue;
      const auto m = wt->GetPlaybackOutputMask();
      occupied.lo |= m.lo;
      occupied.hi |= m.hi;
   }
   return occupied;
}

//! Find the lowest channel >= 0 such that @p channelCount consecutive
//! channels starting at that index are all zero in @p occupied.
//! Returns kPlaybackOutputMaskBits if no slot fits (mask is full).
unsigned NextFreeSlot(
   const PlaybackOutputMask& occupied, unsigned channelCount)
{
   if (channelCount == 0)
      return 0;
   for (unsigned start = 0;
        start + channelCount <= kPlaybackOutputMaskBits;
        ++start)
   {
      bool fits = true;
      for (unsigned n = 0; n < channelCount; ++n) {
         if (occupied.test(start + n)) {
            fits = false;
            break;
         }
      }
      if (fits)
         return start;
   }
   return kPlaybackOutputMaskBits;
}

//! Assign identity routing to @p track starting at the next free slot
//! relative to the other tracks in @p list.  No-op if @p track already
//! has a non-empty mask or no free slot remains.
void AssignIdentityIfEmpty(WaveTrack& track, const TrackList& list)
{
   if (!track.GetPlaybackOutputMask().empty())
      return;
   const auto occupied = OccupiedBits(list, &track);
   const auto channels = track.NChannels();
   if (channels == 0)
      return;
   const auto start = NextFreeSlot(occupied, static_cast<unsigned>(channels));
   if (start >= kPlaybackOutputMaskBits)
      return; // mask full; leave track silent (user can reconfigure)
   track.SetPlaybackOutputMask(
   PlaybackOutputMask::Identity(
         start, static_cast<unsigned>(channels)));
}

//! Mirror of OccupiedBits / NextFreeSlot / AssignIdentityIfEmpty for
//! the recording-input side.  Identity routing for input means
//! "this track records from device input bits at its row position";
//! we keep input and output lanes independent because a user might
//! configure them separately, and an input slot that's occupied by
//! another track's recording shouldn't also be claimed.
PlaybackInputMask OccupiedInputBits(
   const TrackList& list, const Track* exclude = nullptr)
{
   PlaybackInputMask occupied;
   for (const auto& track : list) {
      if (track == exclude)
         continue;
      const auto* wt = dynamic_cast<const WaveTrack*>(track);
      if (!wt)
         continue;
      const auto m = wt->GetPlaybackInputMask();
      occupied.lo |= m.lo;
      occupied.hi |= m.hi;
   }
   return occupied;
}

unsigned NextFreeInputSlot(
   const PlaybackInputMask& occupied, unsigned channelCount)
{
   if (channelCount == 0)
      return 0;
   for (unsigned start = 0;
        start + channelCount <= kPlaybackInputMaskBits;
        ++start)
   {
      bool fits = true;
      for (unsigned n = 0; n < channelCount; ++n) {
         if (occupied.test(start + n)) {
            fits = false;
            break;
         }
      }
      if (fits)
         return start;
   }
   return kPlaybackInputMaskBits;
}

void AssignIdentityInputIfEmpty(WaveTrack& track, const TrackList& list)
{
   if (!track.GetPlaybackInputMask().empty())
      return;
   const auto occupied = OccupiedInputBits(list, &track);
   const auto channels = track.NChannels();
   if (channels == 0)
      return;
   const auto start =
      NextFreeInputSlot(occupied, static_cast<unsigned>(channels));
   if (start >= kPlaybackInputMaskBits)
      return; // mask full; leave track without an input route
   track.SetPlaybackInputMask(
      PlaybackInputMask::Identity(
         start, static_cast<unsigned>(channels)));
}

class PlaybackRoutingListener final : public ClientData::Base
{
public:
   PlaybackRoutingListener(AudacityProject&, TrackList& trackList)
      : mTrackList{ trackList }
   {
      // Load-time migration: for tracks loaded from projects that had
      // NO outputmask* attribute (upstream / pre-feature), stamp in
      // identity routing.  Tracks whose attr flag was set keep
      // whatever mask the XML specified (including empty = silent).
      WalkListOnce();

      // Live-assignment: any time a new WaveTrack lands in the list,
      // make sure it has a mask.
      mSubscription = trackList.Subscribe(
         [this](const TrackListEvent& event) {
            if (event.mType != TrackListEvent::ADDITION)
               return;
            const auto track = event.mpTrack.lock();
            if (!track)
               return;
            if (auto* wt = dynamic_cast<WaveTrack*>(track.get())) {
               AssignIdentityIfEmpty(*wt, mTrackList);
               AssignIdentityInputIfEmpty(*wt, mTrackList);
            }
         });
   }

private:
   void WalkListOnce()
   {
      // Passes over the list can't be order-dependent: each track is
      // considered in list order, and the identity assignment for
      // track K sees the mask choices already made by tracks 0..K-1.
      // For brand-new projects the list is empty; this is a no-op.
      for (auto* track : mTrackList) {
         auto* wt = dynamic_cast<WaveTrack*>(track);
         if (!wt)
            continue;
         // Output side migration.
         if (!wt->GetOutputMaskAttrSeen()) {
            AssignIdentityIfEmpty(*wt, mTrackList);
            wt->SetOutputMaskAttrSeen(true);
         }
         // Input side migration.  Tracks that came from upstream /
         // pre-feature projects had no inputmask* attrs; we
         // synthesize identity so they are recordable by default
         // under the matrix model.  Tracks that DID see inputmask
         // attrs keep whatever they were saved with (including
         // empty == not a recording target).
         if (!wt->GetInputMaskAttrSeen()) {
            AssignIdentityInputIfEmpty(*wt, mTrackList);
            wt->SetInputMaskAttrSeen(true);
         }
      }
   }

   TrackList& mTrackList;
   Observer::Subscription mSubscription;
};

static const AttachedProjectObjects::RegisteredFactory key {
   [](AudacityProject& project) {
      return std::make_shared<PlaybackRoutingListener>(
         project, TrackList::Get(project));
   }
};

} // namespace
