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

#include <wx/defs.h>     // wxID_HIGHEST
#include <wx/scrolwin.h> // wxScrolledCanvas is a template alias

#include <vector>

//! ShowModal() return code used by both the playback and recording
//! routing dialogs to signal "the user just created tracks via the
//! empty-state form, please reopen the dialog so they can configure
//! the freshly-created masks".  Toolbar handlers loop on this value.
//!
//! Defined in one place to keep the dialog and its caller in
//! lockstep -- a drift between the two would silently break the
//! auto-reopen contract.
constexpr int kRoutingDialogReopenAfterCreate = wxID_HIGHEST + 1;

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

   //! Reposition the children of the (non-scrolling) header and
   //! labels panels to match the matrix's current scroll offset.
   //! Called from the matrix scroll handler and the dialog resize
   //! handler.
   void SyncHeaderAndLabelPositions();

   AudacityProject &mProject;
   //! Number of physical device output channels (used for marking
   //! "off-device" columns and for the status footer).
   size_t mNumDeviceChannels = 0;
   //! Number of columns the dialog actually renders.  >= device count
   //! when any track's mask has bits past the device width, so those
   //! bits remain visible / clearable.  Computed by
   //! ComputeRoutingDialogColumnCount.
   size_t mNumOutputChannels = 0;
   std::vector<TrackRow> mRows;

   // Geometry constants filled in ComputeInitialSize and reused by
   // BuildUI so the sub-panels line up pixel-for-pixel.
   int mColumnWidth = 28;     //!< pixels per matrix column
   //! Pixels per matrix row.  Sized to the natural height of a
   //! wxButton on GTK -- forcing rows shorter ellipsizes the Reset
   //! button's label.
   int mRowHeight = 32;
   int mLabelColWidth = 220;  //!< left-frozen column width (label + Reset)
   int mHeaderRowHeight = 28; //!< top-frozen row height

   //! Header and labels are plain wxPanels; their children are
   //! positioned manually via SyncHeaderAndLabelPositions so they
   //! never go through wxScrolledWindow's blit/realize/unrealize
   //! paths -- those caused stale Reset buttons after resize+scroll
   //! when these were wxScrolledCanvas.
   wxPanel *mCornerPanel = nullptr;
   wxPanel *mHeaderPanel = nullptr;
   wxPanel *mLabelsPanel = nullptr;
   //! Per-row label children kept around so we can move them on
   //! every scroll.  Same length as mRows.
   struct LabelRow {
      wxStaticText *text = nullptr;
      wxButton *resetBtn = nullptr;
   };
   std::vector<LabelRow> mLabelRows;
   //! Per-channel header label children, similarly.
   std::vector<wxStaticText *> mHeaderLabels;

   //! Matrix is the only panel that actually scrolls.
   wxScrolledCanvas *mMatrixPanel = nullptr;
   wxStaticText *mStatusText = nullptr;
};
