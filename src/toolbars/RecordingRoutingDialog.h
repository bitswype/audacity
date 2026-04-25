/**********************************************************************

  Audacity: A Digital Audio Editor

  RecordingRoutingDialog.h

  bitswype fork: N tracks x M device input channels routing matrix
  dialog.  Each row is a track; each column is a device INPUT channel.
  Check a box to route that input channel into the row's recording.
  Empty row (no boxes checked) means the track is not a recording
  target.  Reset on a row assigns identity routing based on the row's
  current position.

  Mono tracks SUM all checked input channels; multi-channel tracks
  walk checked bits 1:1 in low-to-high order to channels 0..N-1, with
  any extras dropped.  See PlaybackInputMask.h.

  Layout mirrors PlaybackRoutingDialog: four-region scroll-synced
  panel grid (corner, header, labels, matrix), with the matrix as
  the only scrolling pane.

**********************************************************************/
#pragma once

#include "PlaybackInputMask.h"
#include "wxPanelWrapper.h"

#include <wx/scrolwin.h>

#include <vector>

class AudacityProject;
class WaveTrack;
class wxCheckBox;
class wxStaticText;
class wxScrollWinEvent;

class RecordingRoutingDialog final : public wxDialogWrapper
{
public:
   //! If @p focusedTrack is non-null, the matrix scrolls to show
   //! that track's row on open.
   RecordingRoutingDialog(wxWindow *parent, AudacityProject &project,
      WaveTrack *focusedTrack = nullptr);

   ~RecordingRoutingDialog() override;

private:
   struct TrackRow {
      WaveTrack *track = nullptr;
      PlaybackInputMask originalStoredMask{};
      std::vector<wxCheckBox *> checks;
   };

   wxSize ComputeInitialSize() const;
   void BuildUI(WaveTrack *focusedTrack);
   void OnClose(wxCommandEvent &);
   void OnResetRow(int rowIndex);
   //! Read checkbox states and write resulting masks back.  Returns
   //! the number of tracks whose stored mask changed.
   int ApplyIntents();
   void UpdateStatus(int rowIndex, int colIndex);
   void OnMatrixScroll(wxScrollWinEvent &event);
   void SyncHeaderAndLabelPositions();

   AudacityProject &mProject;
   //! Number of physical device input channels (used for marking
   //! "off-device" columns and for the status footer).
   size_t mNumDeviceChannels = 0;
   //! Number of columns the dialog actually renders.  Computed by
   //! ComputeRecordingDialogColumnCount.
   size_t mNumOutputChannels = 0;
   std::vector<TrackRow> mRows;

   int mColumnWidth = 28;
   int mRowHeight = 32;
   int mLabelColWidth = 220;
   int mHeaderRowHeight = 28;

   wxPanel *mCornerPanel = nullptr;
   wxPanel *mHeaderPanel = nullptr;
   wxPanel *mLabelsPanel = nullptr;
   struct LabelRow {
      wxStaticText *text = nullptr;
      wxButton *resetBtn = nullptr;
   };
   std::vector<LabelRow> mLabelRows;
   std::vector<wxStaticText *> mHeaderLabels;

   wxScrolledCanvas *mMatrixPanel = nullptr;
   wxStaticText *mStatusText = nullptr;
};
