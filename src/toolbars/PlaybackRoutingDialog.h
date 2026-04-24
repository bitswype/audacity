/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.h

  bitswype fork: N tracks x M output channels routing matrix dialog.
  Each row is a track; each column is a device output channel.  Check
  a box to route the row's audio to that channel.  Empty row (no boxes
  checked) means the track is silent.  Reset on a row sets its mask
  to identity routing based on the row's current position.

  Layout uses four sub-regions with scroll synchronization so that
  column headers and track-name labels stay visible when the matrix
  is scrolled:

       +-----------+-----------------------------+
       | corner    | column header (H-scroll)    |
       +-----------+-----------------------------+
       | labels    | matrix (both scrolls)       |
       | (V-scroll)|                             |
       +-----------+-----------------------------+

  Only the matrix shows real scrollbars; the header and labels
  scroll programmatically to stay in sync.  A status line below the
  grid echoes the currently-hovered or focused cell as "Track N
  ('name'), output M".

  After the 128-bit static-mask refactor, every track has exactly
  one PlaybackOutputMask; empty = silent.  Close applies all pending
  changes and dismisses the dialog.

**********************************************************************/
#pragma once

#include "PlaybackOutputMask.h"
#include "wxPanelWrapper.h"

#include <wx/scrolwin.h> // wxScrolledCanvas is a template alias

#include <vector>

class AudacityProject;
class WaveTrack;
class wxCheckBox;
class wxStaticText;
class wxScrollWinEvent;

class PlaybackRoutingDialog final : public wxDialogWrapper
{
public:
   //! If @p focusedTrack is non-null, the matrix scrolls to show
   //! that track's row on open (used when the dialog is launched
   //! from a right-click on one track).
   PlaybackRoutingDialog(wxWindow *parent, AudacityProject &project,
      WaveTrack *focusedTrack = nullptr);

   ~PlaybackRoutingDialog() override;

private:
   struct TrackRow {
      WaveTrack *track = nullptr;
      PlaybackOutputMask originalStoredMask{};
      std::vector<wxCheckBox *> checks; //!< one per displayed output channel
   };

   //! Size the dialog for the row/column counts we're about to
   //! render, capped at a fraction of the screen so it always fits.
   wxSize ComputeInitialSize() const;
   void BuildUI(WaveTrack *focusedTrack);
   void OnClose(wxCommandEvent &);
   //! Per-row Reset: assign identity routing for this row at bits
   //! starting at its track index (clamped to device width).
   void OnResetRow(int rowIndex);
   //! Read checkbox states and write resulting masks back.  Returns
   //! the number of tracks whose stored mask changed.
   int ApplyIntents();
   //! Update the status line at the bottom.  @p rowIndex < 0 means
   //! "clear".
   void UpdateStatus(int rowIndex, int colIndex);
   //! Called when the main matrix scrolls; propagates the position
   //! to the header (horizontal) and labels (vertical) sub-regions.
   void OnMatrixScroll(wxScrollWinEvent &event);

   AudacityProject &mProject;
   size_t mNumOutputChannels = 0;
   std::vector<TrackRow> mRows;

   // Geometry constants filled in ComputeInitialSize and reused by
   // BuildUI so the sub-panels line up pixel-for-pixel.
   int mColumnWidth = 28;     //!< pixels per matrix column
   int mRowHeight = 24;       //!< pixels per matrix row
   int mLabelColWidth = 220;  //!< left-frozen column width (label + Reset)
   int mHeaderRowHeight = 24; //!< top-frozen row height

   wxScrolledCanvas *mCornerPanel = nullptr;
   wxScrolledCanvas *mHeaderPanel = nullptr;
   wxScrolledCanvas *mLabelsPanel = nullptr;
   wxScrolledCanvas *mMatrixPanel = nullptr;
   wxStaticText *mStatusText = nullptr;
};
