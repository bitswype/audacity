/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.h

  bitswype fork: N tracks x M output channels routing matrix dialog.
  Each row is a track; each column is a device output channel.  Check a
  box to route the row's audio to that channel.  Empty row (no boxes
  checked) means the track is silent.  Reset on a row sets its mask to
  identity routing based on the row's current position in the track
  list.

  After the 128-bit static-mask refactor, the dialog no longer tracks
  a per-row "Intent" (Auto / Explicit / Silent).  Every row has exactly
  one mask; empty = silent.

  Close applies all pending changes and dismisses the dialog.

**********************************************************************/
#pragma once

#include "PlaybackOutputMask.h"
#include "wxPanelWrapper.h"

#include <vector>

class AudacityProject;
class WaveTrack;
class wxCheckBox;
class wxScrolledWindow;

class PlaybackRoutingDialog final : public wxDialogWrapper
{
public:
   //! If @p focusedTrack is non-null, the matrix scrolls to show that
   //! track's row on open (used when the dialog is launched from a
   //! right-click on one track).
   PlaybackRoutingDialog(wxWindow *parent, AudacityProject &project,
      WaveTrack *focusedTrack = nullptr);

   ~PlaybackRoutingDialog() override;

private:
   struct TrackRow {
      WaveTrack *track = nullptr;
      PlaybackOutputMask originalStoredMask{};
      std::vector<wxCheckBox *> checks; //!< one per displayed output channel
   };

   void BuildUI(WaveTrack *focusedTrack);
   void OnClose(wxCommandEvent &);
   //! Per-row Reset: assign identity routing for this row at the
   //! channels starting at its track index (clamped to the current
   //! device width).  Stereo/mono/multi spread consecutive bits.
   void OnResetRow(int rowIndex);
   //! Read checkbox states and write resulting masks back to the
   //! tracks.  Returns the number of tracks whose stored mask changed.
   int ApplyIntents();

   AudacityProject &mProject;
   size_t mNumOutputChannels = 0;
   std::vector<TrackRow> mRows;
   wxScrolledWindow *mMatrixPanel = nullptr;
};
