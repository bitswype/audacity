/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.h

  bitswype fork: N tracks x M output channels routing matrix dialog.
  Lets the user explicitly map each WaveTrack to one or more device
  output channels, supporting 1-to-1, 1-to-many, many-to-1, and
  1-to-none mappings.  Setting empty mask restores default identity
  routing.

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
   //! If @p focusedTrack is non-null, the matrix scrolls to show that
   //! track's row on open (used when the dialog is launched from a
   //! right-click on one track).
   PlaybackRoutingDialog(wxWindow *parent, AudacityProject &project,
      WaveTrack *focusedTrack = nullptr);

   ~PlaybackRoutingDialog() override;

private:
   struct TrackRow {
      WaveTrack *track;
      uint64_t originalMask;          //!< value before the dialog opened
      std::vector<wxCheckBox *> checks; //!< one per output channel
   };

   void BuildUI(WaveTrack *focusedTrack);
   void OnApply(wxCommandEvent &);
   void OnClose(wxCommandEvent &);
   //! Clear one row's checkboxes (restore identity routing for that
   //! track).
   void OnResetRow(int rowIndex);
   //! Read each row's checkbox state and apply as the track's mask.
   //! Returns the number of tracks whose mask changed.
   int ApplyMasks();

   AudacityProject &mProject;
   size_t mNumOutputChannels;
   std::vector<TrackRow> mRows;
   wxScrolledWindow *mMatrixPanel{};
};
