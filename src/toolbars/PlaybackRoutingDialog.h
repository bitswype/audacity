/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.h

  bitswype fork: N tracks x M output channels routing matrix dialog.
  Each row shows the current effective routing for a track (including
  the auto-routing that would apply if the user had not configured
  anything).  Three per-row intents:

    - Auto     : engine picks the outputs (identity routing for
                 multi-channel devices; legacy stereo duplication
                 otherwise).  This is the default for new tracks and
                 what Reset restores.
    - Explicit : the user has picked specific outputs via the
                 checkboxes.  At least one checkbox is checked.
    - Silent   : the user unchecked all of a row's boxes.  The track
                 is explicitly silenced (distinct from Auto).

  Close applies all pending changes and dismisses the dialog.

**********************************************************************/
#pragma once

#include "wxPanelWrapper.h"

#include <cstdint>
#include <vector>

class AudacityProject;
class WaveTrack;
class wxCheckBox;
class wxScrolledWindow;

class PlaybackRoutingDialog final : public wxDialogWrapper
{
public:
   //! Per-row playback intent.  Exposed so helpers in the .cpp's
   //! anonymous namespace can construct Intent values -- it is not
   //! part of the user-facing API, keep as an implementation detail.
   enum class Intent {
      Auto,     //!< engine-chosen routing; stored as mask = 0
      Explicit, //!< user-chosen bits; stored as that mask
      Silent,   //!< explicit no-playback; stored as the silent sentinel
   };

   //! If @p focusedTrack is non-null, the matrix scrolls to show that
   //! track's row on open (used when the dialog is launched from a
   //! right-click on one track).
   PlaybackRoutingDialog(wxWindow *parent, AudacityProject &project,
      WaveTrack *focusedTrack = nullptr);

   ~PlaybackRoutingDialog() override;

private:
   struct TrackRow {
      WaveTrack *track = nullptr;
      uint64_t originalStoredMask = 0; //!< value on the track when dialog opened
      Intent intent = Intent::Auto;
      std::vector<wxCheckBox *> checks; //!< one per output channel
   };

   void BuildUI(WaveTrack *focusedTrack);
   void OnClose(wxCommandEvent &);
   //! Per-row Reset: mark the row Auto and re-check the boxes that
   //! correspond to the current effective auto routing.
   void OnResetRow(int rowIndex);
   //! Any checkbox toggled: recompute the row's Intent from the
   //! resulting box state (Silent if all unchecked, Explicit
   //! otherwise).
   void OnRowCheckboxToggled(int rowIndex);
   //! Compute the effective auto-routing bitmask for the given row,
   //! using the current state of all tracks (so an auto track that
   //! shares outputs with other explicit tracks sees the real routing
   //! the engine would use).  Used both during dialog population and
   //! for the Reset button.
   uint64_t ComputeAutoMaskForRow(int rowIndex) const;
   //! Read checkbox states and write resulting masks back to the
   //! tracks.  Returns the number of tracks whose stored mask changed.
   int ApplyIntents();

   AudacityProject &mProject;
   size_t mNumOutputChannels = 0;
   std::vector<TrackRow> mRows;
   wxScrolledWindow *mMatrixPanel = nullptr;
};
