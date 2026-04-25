/**********************************************************************

  Audacity: A Digital Audio Editor

  RecordingRoutingDialog.cpp

**********************************************************************/

#include "RecordingRoutingDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "AudioIOBase.h"        // for AudioIORecordChannels
#include "PlaybackInputMask.h"
#include "Project.h"
#include "ProjectHistory.h"
#include "ShuttleGui.h"
#include "Track.h"
#include "WaveTrack.h"

#include <algorithm>

namespace {
//! Upper bound on columns the dialog will ever display.  Matches the
//! width of the underlying mask.
constexpr size_t kMaxDisplayChannels = kPlaybackInputMaskBits;

//! Scroll unit in pixels for all four sub-canvases.  Must be shared
//! so scroll-position sync between them is a one-to-one unit mapping.
constexpr int kScrollUnit = 1;

//! Read the first @p numChannels checkboxes into a PlaybackInputMask.
PlaybackInputMask MaskFromCheckboxes(
   const std::vector<wxCheckBox*>& checks, size_t numChannels)
{
   PlaybackInputMask mask;
   const auto cap = std::min(
      std::min(checks.size(), numChannels), kMaxDisplayChannels);
   for (size_t i = 0; i < cap; ++i)
      if (checks[i] && checks[i]->GetValue())
         mask.set(static_cast<unsigned>(i));
   return mask;
}

//! Set the first @p numChannels checkboxes to match a mask.
void CheckboxesFromMask(
   const std::vector<wxCheckBox*>& checks,
   const PlaybackInputMask& mask, size_t numChannels)
{
   const auto cap = std::min(
      std::min(checks.size(), numChannels), kMaxDisplayChannels);
   for (size_t i = 0; i < cap; ++i)
      if (checks[i])
         checks[i]->SetValue(mask.test(static_cast<unsigned>(i)));
}
} // namespace

RecordingRoutingDialog::RecordingRoutingDialog(
   wxWindow *parent, AudacityProject &project, WaveTrack *focusedTrack)
   : wxDialogWrapper(parent, wxID_ANY,
                     XO("Recording Routing Matrix"),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mProject(project)
{
   SetName();

   auto requested = AudioIORecordChannels.ReadWithDefault(2);
   if (requested < 1)
      requested = 1;
   if (requested > static_cast<int>(kMaxDisplayChannels))
      requested = static_cast<int>(kMaxDisplayChannels);
   mNumDeviceChannels = static_cast<size_t>(requested);

   // Seed rows up front so BuildUI and ComputeInitialSize both see
   // the track list.
   auto &tracks = TrackList::Get(mProject);
   for (auto pTrack : tracks.Any<WaveTrack>()) {
      TrackRow row;
      row.track = pTrack;
      row.originalStoredMask = pTrack->GetPlaybackInputMask();
      mRows.push_back(std::move(row));
   }

   // Choose how many columns to render.  At minimum the device
   // width, but we extend the visible range to cover any track
   // mask bits beyond it (so the user can see and clear them) and
   // to leave room for Reset to assign identity routing on the
   // last row.  See ComputeRecordingDialogColumnCount.
   std::vector<PlaybackInputMask> masks;
   std::vector<unsigned> chans;
   masks.reserve(mRows.size());
   chans.reserve(mRows.size());
   for (const auto &row : mRows) {
      masks.push_back(row.originalStoredMask);
      chans.push_back(row.track
         ? static_cast<unsigned>(row.track->NChannels())
         : 0u);
   }
   mNumOutputChannels = ComputeRecordingDialogColumnCount(
      static_cast<unsigned>(mNumDeviceChannels), masks, chans);

   SetClientSize(ComputeInitialSize());
   BuildUI(focusedTrack);
}

RecordingRoutingDialog::~RecordingRoutingDialog() = default;

wxSize RecordingRoutingDialog::ComputeInitialSize() const
{
   // Rough pixel estimates.  These do not need to match the final
   // widget sizes exactly -- the matrix is scrollable, so being a
   // little off just changes how much scrolling the user has to do.

   // Add instruction text + status line + close button padding.
   constexpr int kChromeVerticalPadding = 170;
   constexpr int kChromeHorizontalPadding = 40;

   const int contentWidth =
      mLabelColWidth +
      static_cast<int>(mNumOutputChannels) * mColumnWidth;
   const int contentHeight =
      mHeaderRowHeight +
      static_cast<int>(mRows.size()) * mRowHeight;

   int w = contentWidth + kChromeHorizontalPadding;
   int h = contentHeight + kChromeVerticalPadding;

   // Cap at 80% of the display that owns the parent (or the primary
   // display if we don't have a parent to query).
   const auto displayIdx = wxDisplay::GetFromWindow(GetParent());
   const wxDisplay display(displayIdx != wxNOT_FOUND ? displayIdx : 0u);
   const auto screen = display.GetClientArea();
   w = std::clamp(w, 480, screen.GetWidth() * 4 / 5);
   h = std::clamp(h, 320, screen.GetHeight() * 4 / 5);
   return wxSize(w, h);
}

void RecordingRoutingDialog::BuildUI(WaveTrack *focusedTrack)
{
   auto *outer = new wxBoxSizer(wxVERTICAL);

   auto *header = new wxStaticText(this, wxID_ANY,
      XO("Each checkbox feeds the corresponding device input channel "
         "into the row's track when recording.\n"
         "Mono tracks SUM all checked inputs.  Multi-channel tracks "
         "take checked inputs in order; extras are dropped.  An "
         "empty row means the track is not a recording target.  "
         "Use Reset to restore identity routing for a row.")
         .Translation());
   outer->Add(header, 0, wxEXPAND | wxALL, 8);

   // If any tracks reference channels beyond the current playback
   // device, surface that explicitly so the user knows why some
   // columns are marked with an asterisk.
   {
      std::vector<PlaybackInputMask> masks;
      masks.reserve(mRows.size());
      for (const auto &row : mRows)
         masks.push_back(row.originalStoredMask);
      const auto offCount = CountRecordingTracksWithBitsAboveDeviceWidth(
         static_cast<unsigned>(mNumDeviceChannels), masks);
      if (offCount > 0) {
         auto *notice = new wxStaticText(this, wxID_ANY,
            wxString::Format(
               _("Note: %u track(s) reference inputs beyond your "
                 "%u-channel recording device.  Off-device columns "
                 "are marked with * and will record silence until you "
                 "switch to a device with more inputs."),
               offCount,
               static_cast<unsigned>(mNumDeviceChannels)));
         notice->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
         outer->Add(notice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
      }
   }

   // ----------------- 4-region scroll-synced layout -----------------
   //
   // +---------+--------------------+---+
   // | corner  | header panel       | sp|  topRow (no scrollbars)
   // +---------+--------------------+---+
   // | labels  | matrix panel       |Vsb|  bodyRow
   // |         | (with scrollbars)  |   |
   // +---------+--------------------+---+
   // | spacer  | Hsb in matrix      |   |
   // +---------+--------------------+---+
   //
   // The matrix is the only panel that displays its scrollbars;
   // they sit at its own right and bottom edges and consume ~17px
   // each from its client area.  To keep header and labels' content
   // areas aligned with the matrix's content area, we add explicit
   // spacer cells next to them whose size matches the scrollbar
   // dimensions.  Without these spacers, labels visually ends 17px
   // higher than the matrix's last row, which makes the bottom row
   // of checkboxes appear "below" the labels column.
   const int kVScrollW =
      wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
   const int kHScrollH =
      wxSystemSettings::GetMetric(wxSYS_HSCROLL_Y);

   // --- Corner panel (fixed, no scroll, never moves) ---
   mCornerPanel = new wxPanel(this, wxID_ANY,
      wxDefaultPosition, wxSize(mLabelColWidth, mHeaderRowHeight),
      wxBORDER_NONE);
   mCornerPanel->SetMinSize(wxSize(mLabelColWidth, mHeaderRowHeight));
   {
      auto *cornerSz = new wxBoxSizer(wxHORIZONTAL);
      cornerSz->Add(new wxStaticText(mCornerPanel, wxID_ANY, _("Track")),
         1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
      mCornerPanel->SetSizer(cornerSz);
   }

   // --- Header panel (plain wxPanel; children moved on scroll) ---
   mHeaderPanel = new wxPanel(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mHeaderPanel->SetMinSize(wxSize(-1, mHeaderRowHeight));
   for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
      // Off-device columns get an asterisk suffix and italic font so
      // they are visually distinct from the device-routable columns.
      const bool offDevice = ch >= mNumDeviceChannels;
      const wxString text = offDevice
         ? wxString::Format("%zu*", ch + 1)
         : wxString::Format("%zu", ch + 1);
      auto *label = new wxStaticText(mHeaderPanel, wxID_ANY, text,
         wxPoint(static_cast<int>(ch) * mColumnWidth, 0),
         wxSize(mColumnWidth, mHeaderRowHeight),
         wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
      if (offDevice) {
         auto font = label->GetFont();
         font.SetStyle(wxFONTSTYLE_ITALIC);
         label->SetFont(font);
         label->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
         label->SetToolTip(
            _("This channel is beyond the current recording device's "
              "input count.  The routing is preserved but will record "
              "silence until the device exposes more channels."));
      }
      mHeaderLabels.push_back(label);
   }

   // --- Labels panel (plain wxPanel; children moved on scroll) ---
   mLabelsPanel = new wxPanel(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mLabelsPanel->SetMinSize(wxSize(mLabelColWidth, -1));

   // --- Matrix panel (both scrolls visible; drives the others) ---
   mMatrixPanel = new wxScrolledCanvas(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mMatrixPanel->SetScrollRate(kScrollUnit, kScrollUnit);
   auto *matrixSz = new wxBoxSizer(wxVERTICAL);
   mMatrixPanel->SetSizer(matrixSz);

   // Assemble the layout now that all four panels exist.  The grid
   // is row-major: top row holds corner+header (no scrollbars), body
   // row holds labels+matrix (matrix has scrollbars at its own
   // bottom and right edges).  Spacer cells reserve scrollbar-equal
   // gaps next to header (right) and labels (bottom) so their
   // content areas line up exactly with the matrix's content area.
   auto *topRow = new wxBoxSizer(wxHORIZONTAL);
   topRow->Add(mCornerPanel, 0);
   topRow->Add(mHeaderPanel, 1, wxEXPAND);
   topRow->AddSpacer(kVScrollW);

   auto *labelsCol = new wxBoxSizer(wxVERTICAL);
   labelsCol->Add(mLabelsPanel, 1, wxEXPAND);
   labelsCol->AddSpacer(kHScrollH);

   auto *bodyRow = new wxBoxSizer(wxHORIZONTAL);
   bodyRow->Add(labelsCol, 0, wxEXPAND);
   bodyRow->Add(mMatrixPanel, 1, wxEXPAND);

   auto *grid = new wxBoxSizer(wxVERTICAL);
   grid->Add(topRow, 0, wxEXPAND);
   grid->Add(bodyRow, 1, wxEXPAND);

   // --- Populate labels + matrix rows ---
   wxWindow *focusTarget = nullptr;
   for (size_t rowIndex = 0; rowIndex < mRows.size(); ++rowIndex) {
      auto &row = mRows[rowIndex];
      const int capturedRow = static_cast<int>(rowIndex);

      // Labels column: track name + Reset button positioned
      // manually so we can move them ourselves on scroll without
      // wxScrolledWindow getting in the way.  Logical y is just
      // rowIndex * mRowHeight; SyncHeaderAndLabelPositions
      // subtracts the matrix's scroll offset on every scroll/resize.
      const int rowY = capturedRow * mRowHeight;
      auto *nameText = new wxStaticText(mLabelsPanel, wxID_ANY,
         row.track->GetName(),
         wxPoint(4, rowY),
         wxSize(mLabelColWidth - 64, mRowHeight),
         wxST_ELLIPSIZE_END | wxALIGN_CENTER_VERTICAL);
      auto *resetBtn = new wxButton(mLabelsPanel, wxID_ANY, _("Reset"),
         wxPoint(mLabelColWidth - 60, rowY + 2),
         wxSize(56, mRowHeight - 4), wxBU_EXACTFIT);
      resetBtn->Bind(wxEVT_BUTTON,
         [this, capturedRow](wxCommandEvent &) {
            this->OnResetRow(capturedRow);
         });
      mLabelRows.push_back(LabelRow{ nameText, resetBtn });

      // Matrix row of checkboxes.  Same fixed-height panel wrapper
      // for the same reason.
      auto *rowPanel = new wxPanel(mMatrixPanel, wxID_ANY,
         wxDefaultPosition,
         wxSize(static_cast<int>(mNumOutputChannels) * mColumnWidth,
                mRowHeight));
      rowPanel->SetMinSize(wxSize(
         static_cast<int>(mNumOutputChannels) * mColumnWidth,
         mRowHeight));
      auto *checkRow = new wxBoxSizer(wxHORIZONTAL);
      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *check = new wxCheckBox(rowPanel, wxID_ANY,
            wxEmptyString,
            wxDefaultPosition, wxSize(mColumnWidth, mRowHeight),
            wxALIGN_CENTER);
         check->SetValue(
            row.originalStoredMask.test(static_cast<unsigned>(ch)));
         const int capturedCol = static_cast<int>(ch);
         check->Bind(wxEVT_ENTER_WINDOW,
            [this, capturedRow, capturedCol](wxMouseEvent &e) {
               this->UpdateStatus(capturedRow, capturedCol);
               e.Skip();
            });
         check->Bind(wxEVT_SET_FOCUS,
            [this, capturedRow, capturedCol](wxFocusEvent &e) {
               this->UpdateStatus(capturedRow, capturedCol);
               e.Skip();
            });
         row.checks.push_back(check);
         checkRow->Add(check, 0);
      }
      rowPanel->SetSizer(checkRow);
      matrixSz->Add(rowPanel, 0);

      if (row.track == focusedTrack)
         focusTarget = row.checks.empty() ? nullptr : row.checks[0];
   }

   // FitInside on the matrix sets its virtual size from the
   // sizer's MinSize, which is what makes its scrollbars appear
   // when the rows/columns overflow the panel's client area.
   // The header and labels are plain wxPanels with manually-
   // positioned children -- they don't need a virtual size.
   mMatrixPanel->FitInside();

   outer->Add(grid, 1, wxEXPAND | wxALL, 6);

   // --- Status line ---
   mStatusText = new wxStaticText(this, wxID_ANY, wxEmptyString,
      wxDefaultPosition, wxDefaultSize,
      wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
   outer->Add(mStatusText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

   // --- Buttons ---
   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   auto *closeBtn = new wxButton(this, wxID_CLOSE, _("&Close"));
   closeBtn->Bind(wxEVT_BUTTON, &RecordingRoutingDialog::OnClose, this);
   closeBtn->SetDefault();
   btnRow->AddStretchSpacer();
   btnRow->Add(closeBtn, 0, wxALL, 6);
   outer->Add(btnRow, 0, wxEXPAND);

   SetSizer(outer);

   // Wire scroll sync last, after all virtual sizes are set.
   // wxEVT_SCROLLWIN fires for scrollbar interaction; we ALSO need
   // to sync after wheel / keyboard scrolls, so the easiest catch-all
   // is to listen for the low-level scroll events on the matrix and
   // re-read its view start every time.
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_TOP,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_BOTTOM,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_LINEUP,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_LINEDOWN,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_PAGEUP,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_PAGEDOWN,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_THUMBTRACK,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_THUMBRELEASE,
      &RecordingRoutingDialog::OnMatrixScroll, this);
   // Mouse-wheel scroll fires wxEVT_SCROLLWIN_LINE{UP,DOWN}, already
   // covered above.  Keyboard arrow-key scroll within the matrix
   // also triggers SCROLLWIN_*, also covered.

   // Re-fit virtual sizes and force a full redraw whenever the
   // dialog is resized.  Without this, the labels' virtual height
   // can lag behind the actual content after a resize, which makes
   // Scroll() clamp wrong on the next scroll event and leaves
   // newly-revealed Reset buttons mis-positioned.
   Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      e.Skip();
      if (mMatrixPanel) mMatrixPanel->FitInside();
      // Reposition header and labels children to match the matrix's
      // (possibly clamped) scroll position after the resize.
      SyncHeaderAndLabelPositions();
   });

   if (focusTarget) {
      int x, y;
      focusTarget->GetPosition(&x, &y);
      mMatrixPanel->Scroll(0, y);
      SyncHeaderAndLabelPositions();
      focusTarget->SetFocus();
   }
}

void RecordingRoutingDialog::OnMatrixScroll(wxScrollWinEvent &event)
{
   event.Skip();
   SyncHeaderAndLabelPositions();
}

void RecordingRoutingDialog::SyncHeaderAndLabelPositions()
{
   // Header and labels are plain (non-scrolling) wxPanels.  We
   // reposition their children to (logical_pos - matrix_scroll).
   // The wxPanel naturally clips drawing to its bounds, so children
   // moved off-panel disappear visually without needing scroll
   // logic.  This avoids the GTK realize/unrealize quirks that
   // wxScrolledWindow's blit-style scroll triggers on resize.
   if (!mMatrixPanel)
      return;
   int vx = 0, vy = 0;
   mMatrixPanel->GetViewStart(&vx, &vy);

   for (size_t ch = 0; ch < mHeaderLabels.size(); ++ch) {
      if (auto *lbl = mHeaderLabels[ch]) {
         const int x = static_cast<int>(ch) * mColumnWidth - vx;
         lbl->Move(x, 0);
      }
   }
   for (size_t i = 0; i < mLabelRows.size(); ++i) {
      const int rowY = static_cast<int>(i) * mRowHeight - vy;
      if (auto *t = mLabelRows[i].text)
         t->Move(4, rowY);
      if (auto *b = mLabelRows[i].resetBtn)
         b->Move(mLabelColWidth - 60, rowY + 2);
   }
}

void RecordingRoutingDialog::UpdateStatus(int rowIndex, int colIndex)
{
   if (!mStatusText)
      return;
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size() ||
       colIndex < 0 ||
       static_cast<size_t>(colIndex) >= mNumOutputChannels)
   {
      mStatusText->SetLabel(wxEmptyString);
      return;
   }
   const auto &row = mRows[rowIndex];
   const wxString name = row.track ? row.track->GetName() : wxString{};
   const bool offDevice =
      static_cast<size_t>(colIndex) >= mNumDeviceChannels;
   if (offDevice)
      mStatusText->SetLabel(wxString::Format(
         _("Track %d ('%s'), input %d (beyond device's "
           "%u channels -- silent)"),
         rowIndex + 1, name, colIndex + 1,
         static_cast<unsigned>(mNumDeviceChannels)));
   else
      mStatusText->SetLabel(wxString::Format(
         _("Track %d ('%s'), input %d"),
         rowIndex + 1, name, colIndex + 1));
}

void RecordingRoutingDialog::OnResetRow(int rowIndex)
{
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return;
   auto &row = mRows[rowIndex];
   const auto channels = row.track ? row.track->NChannels() : size_t{0};
   if (channels == 0)
      return;

   // Compute lowest channel slot whose [start, start+channels) range
   // is unoccupied by any OTHER row's CURRENT checkbox state.  See
   // PlaybackRoutingDialog::OnResetRow for the design rationale.
   PlaybackInputMask occupied;
   for (size_t r = 0; r < mRows.size(); ++r) {
      if (static_cast<int>(r) == rowIndex)
         continue;
      const auto m = MaskFromCheckboxes(
         mRows[r].checks, mNumOutputChannels);
      occupied.lo |= m.lo;
      occupied.hi |= m.hi;
   }
   unsigned start = 0;
   const unsigned want = static_cast<unsigned>(channels);
   while (start + want <= kMaxDisplayChannels) {
      bool fits = true;
      for (unsigned n = 0; n < want; ++n) {
         if (occupied.test(start + n)) { fits = false; break; }
      }
      if (fits)
         break;
      ++start;
   }
   if (start + want > kMaxDisplayChannels)
      return;

   const auto identity = PlaybackInputMask::Identity(start, want);
   CheckboxesFromMask(row.checks, identity, mNumOutputChannels);
}

int RecordingRoutingDialog::ApplyIntents()
{
   // Every set bit on the track is visible as a checkbox: the dialog
   // expands its column range to cover bits beyond the device width
   // (see ComputeRecordingDialogColumnCount).  So the new mask is just
   // whatever the visible checkboxes say -- no invisible-bit
   // preservation needed.
   int changed = 0;
   for (auto &row : mRows) {
      if (!row.track)
         continue;
      const auto newMask =
         MaskFromCheckboxes(row.checks, mNumOutputChannels);
      if (newMask != row.originalStoredMask) {
         row.track->SetPlaybackInputMask(newMask);
         row.originalStoredMask = newMask;
         ++changed;
      }
   }
   return changed;
}

void RecordingRoutingDialog::OnClose(wxCommandEvent &)
{
   const int changed = ApplyIntents();
   // Routing changes are user-visible structural state -- push a
   // single undo step covering all rows the dialog modified, so
   // Ctrl+Z reverts the whole batch.  The full project state
   // (including each WaveTrack's mask attachment) is snapshotted
   // by UndoManager, so undo restores the prior masks even though
   // we hold no explicit before/after vector here.
   if (changed > 0) {
      ProjectHistory::Get(mProject).PushState(
         (changed == 1)
            ? XO("Changed recording routing for 1 track")
            : XO("Changed recording routing for %d tracks").Format(changed),
         XO("Recording Routing"));
   }
   EndModal(wxID_OK);
}
