/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.cpp

**********************************************************************/

#include "PlaybackRoutingDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "AudioIOBase.h"        // for AudioIOPlaybackChannels
#include "PlaybackOutputMask.h"
#include "Project.h"
#include "ShuttleGui.h"
#include "Track.h"
#include "WaveTrack.h"

#include <algorithm>

namespace {
//! Upper bound on columns the dialog will ever display.  Matches the
//! width of the underlying mask.
constexpr size_t kMaxDisplayChannels = kPlaybackOutputMaskBits;

//! Scroll unit in pixels for all four sub-canvases.  Must be shared
//! so scroll-position sync between them is a one-to-one unit mapping.
constexpr int kScrollUnit = 1;

//! Read the first @p numChannels checkboxes into a PlaybackOutputMask.
PlaybackOutputMask MaskFromCheckboxes(
   const std::vector<wxCheckBox*>& checks, size_t numChannels)
{
   PlaybackOutputMask mask;
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
   const PlaybackOutputMask& mask, size_t numChannels)
{
   const auto cap = std::min(
      std::min(checks.size(), numChannels), kMaxDisplayChannels);
   for (size_t i = 0; i < cap; ++i)
      if (checks[i])
         checks[i]->SetValue(mask.test(static_cast<unsigned>(i)));
}
} // namespace

PlaybackRoutingDialog::PlaybackRoutingDialog(
   wxWindow *parent, AudacityProject &project, WaveTrack *focusedTrack)
   : wxDialogWrapper(parent, wxID_ANY,
                     XO("Playback Routing Matrix"),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mProject(project)
{
   SetName();

   auto requested = AudioIOPlaybackChannels.ReadWithDefault(2);
   if (requested < 1)
      requested = 1;
   if (requested > static_cast<int>(kMaxDisplayChannels))
      requested = static_cast<int>(kMaxDisplayChannels);
   mNumOutputChannels = static_cast<size_t>(requested);

   // Seed rows up front so BuildUI and ComputeInitialSize both see
   // the track list.
   auto &tracks = TrackList::Get(mProject);
   for (auto pTrack : tracks.Any<WaveTrack>()) {
      TrackRow row;
      row.track = pTrack;
      row.originalStoredMask = pTrack->GetPlaybackOutputMask();
      mRows.push_back(std::move(row));
   }

   SetClientSize(ComputeInitialSize());
   BuildUI(focusedTrack);
}

PlaybackRoutingDialog::~PlaybackRoutingDialog() = default;

wxSize PlaybackRoutingDialog::ComputeInitialSize() const
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

void PlaybackRoutingDialog::BuildUI(WaveTrack *focusedTrack)
{
   auto *outer = new wxBoxSizer(wxVERTICAL);

   auto *header = new wxStaticText(this, wxID_ANY,
      XO("Each checkbox routes a track's audio to the corresponding "
         "device output channel.\n"
         "An empty row means the track is silenced (no playback). "
         "Use Reset to restore identity routing for a row.")
         .Translation());
   outer->Add(header, 0, wxEXPAND | wxALL, 8);

   // ----------------- 2x2 grid of scrolled canvases -----------------
   //
   // +---------+----------------------+
   // | corner  | column-header panel  |  (horizontally synced)
   // +---------+----------------------+
   // | labels  | matrix panel         |  (vertically synced)
   // | panel   | (the only one with   |
   // |         |  visible scrollbars) |
   // +---------+----------------------+
   //
   // The header / labels panels have their own scrollbars hidden;
   // they scroll programmatically via OnMatrixScroll.
   // The corner is fixed (never scrolls).

   auto *grid = new wxFlexGridSizer(/*cols=*/2, 0, 0);
   grid->AddGrowableCol(1, 1);
   grid->AddGrowableRow(1, 1);

   // --- Corner panel (fixed, no scroll) ---
   mCornerPanel = new wxScrolledCanvas(this, wxID_ANY,
      wxDefaultPosition, wxSize(mLabelColWidth, mHeaderRowHeight),
      wxBORDER_NONE);
   mCornerPanel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
   mCornerPanel->SetMinSize(wxSize(mLabelColWidth, mHeaderRowHeight));
   {
      auto *cornerSz = new wxBoxSizer(wxHORIZONTAL);
      cornerSz->Add(new wxStaticText(mCornerPanel, wxID_ANY, _("Track")),
         1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
      mCornerPanel->SetSizer(cornerSz);
   }
   grid->Add(mCornerPanel, 0, wxEXPAND);

   // --- Header panel (horizontal scroll only, scrollbar hidden) ---
   mHeaderPanel = new wxScrolledCanvas(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mHeaderPanel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
   mHeaderPanel->SetScrollRate(kScrollUnit, 0);
   mHeaderPanel->SetMinSize(wxSize(-1, mHeaderRowHeight));
   {
      auto *headerSz = new wxBoxSizer(wxHORIZONTAL);
      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *label = new wxStaticText(mHeaderPanel, wxID_ANY,
            wxString::Format("%zu", ch + 1),
            wxDefaultPosition, wxSize(mColumnWidth, mHeaderRowHeight),
            wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
         headerSz->Add(label, 0);
      }
      mHeaderPanel->SetSizer(headerSz);
      mHeaderPanel->SetVirtualSize(
         static_cast<int>(mNumOutputChannels) * mColumnWidth,
         mHeaderRowHeight);
   }
   grid->Add(mHeaderPanel, 1, wxEXPAND);

   // --- Labels panel (vertical scroll only, scrollbar hidden) ---
   mLabelsPanel = new wxScrolledCanvas(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mLabelsPanel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
   mLabelsPanel->SetScrollRate(0, kScrollUnit);
   mLabelsPanel->SetMinSize(wxSize(mLabelColWidth, -1));
   auto *labelsSz = new wxBoxSizer(wxVERTICAL);
   mLabelsPanel->SetSizer(labelsSz);
   grid->Add(mLabelsPanel, 0, wxEXPAND);

   // --- Matrix panel (both scrolls visible; drives the others) ---
   mMatrixPanel = new wxScrolledCanvas(this, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
   mMatrixPanel->SetScrollRate(kScrollUnit, kScrollUnit);
   auto *matrixSz = new wxBoxSizer(wxVERTICAL);
   mMatrixPanel->SetSizer(matrixSz);
   grid->Add(mMatrixPanel, 1, wxEXPAND);

   // --- Populate labels + matrix rows ---
   wxWindow *focusTarget = nullptr;
   for (size_t rowIndex = 0; rowIndex < mRows.size(); ++rowIndex) {
      auto &row = mRows[rowIndex];
      const int capturedRow = static_cast<int>(rowIndex);

      // Labels column: track name + inline Reset button.
      auto *labelRow = new wxBoxSizer(wxHORIZONTAL);
      auto *nameText = new wxStaticText(mLabelsPanel, wxID_ANY,
         row.track->GetName(),
         wxDefaultPosition, wxSize(-1, mRowHeight),
         wxST_ELLIPSIZE_END | wxALIGN_CENTER_VERTICAL);
      labelRow->Add(nameText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
      auto *resetBtn = new wxButton(mLabelsPanel, wxID_ANY, _("Reset"),
         wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
      resetBtn->Bind(wxEVT_BUTTON,
         [this, capturedRow](wxCommandEvent &) {
            this->OnResetRow(capturedRow);
         });
      labelRow->Add(resetBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);
      labelsSz->Add(labelRow, 0, wxEXPAND);

      // Matrix row of checkboxes.
      auto *checkRow = new wxBoxSizer(wxHORIZONTAL);
      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *check = new wxCheckBox(mMatrixPanel, wxID_ANY,
            wxEmptyString,
            wxDefaultPosition, wxSize(mColumnWidth, mRowHeight),
            wxALIGN_CENTER);
         check->SetValue(
            row.originalStoredMask.test(static_cast<unsigned>(ch)));
         const int capturedCol = static_cast<int>(ch);
         // Update the status line on hover and keyboard focus.
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
      matrixSz->Add(checkRow, 0);

      if (row.track == focusedTrack)
         focusTarget = row.checks.empty() ? nullptr : row.checks[0];
   }

   // Tell scrolled windows their virtual size so scrollbars size
   // correctly.
   const int matrixVirtW =
      static_cast<int>(mNumOutputChannels) * mColumnWidth;
   const int matrixVirtH =
      static_cast<int>(mRows.size()) * mRowHeight;
   mMatrixPanel->SetVirtualSize(matrixVirtW, matrixVirtH);
   mLabelsPanel->SetVirtualSize(mLabelColWidth, matrixVirtH);
   mHeaderPanel->SetVirtualSize(matrixVirtW, mHeaderRowHeight);

   outer->Add(grid, 1, wxEXPAND | wxALL, 6);

   // --- Status line ---
   mStatusText = new wxStaticText(this, wxID_ANY, wxEmptyString,
      wxDefaultPosition, wxDefaultSize,
      wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
   outer->Add(mStatusText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

   // --- Buttons ---
   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   auto *closeBtn = new wxButton(this, wxID_CLOSE, _("&Close"));
   closeBtn->Bind(wxEVT_BUTTON, &PlaybackRoutingDialog::OnClose, this);
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
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_BOTTOM,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_LINEUP,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_LINEDOWN,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_PAGEUP,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_PAGEDOWN,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_THUMBTRACK,
      &PlaybackRoutingDialog::OnMatrixScroll, this);
   mMatrixPanel->Bind(wxEVT_SCROLLWIN_THUMBRELEASE,
      &PlaybackRoutingDialog::OnMatrixScroll, this);

   if (focusTarget) {
      int x, y;
      focusTarget->GetPosition(&x, &y);
      mMatrixPanel->Scroll(0, y);
      mLabelsPanel->Scroll(0, y);
      focusTarget->SetFocus();
   }
}

void PlaybackRoutingDialog::OnMatrixScroll(wxScrollWinEvent &event)
{
   // Let the matrix process the scroll normally, then propagate the
   // resulting view position to the header (horizontal) and labels
   // (vertical) panels.  GetViewStart reports in scroll units; since
   // we set kScrollUnit = 1 on all panels, the units are pixels.
   event.Skip();
   // Defer reading the view start until the matrix has updated;
   // CallAfter is the usual way.  But for hidden-scrollbar linked
   // panels a direct read works in practice on all three platforms
   // we build for.
   int vx = 0, vy = 0;
   mMatrixPanel->GetViewStart(&vx, &vy);
   // Apply to siblings.  wxScrolledCanvas::Scroll clamps out-of-range
   // positions against the virtual size.
   mHeaderPanel->Scroll(vx, 0);
   mLabelsPanel->Scroll(0, vy);
}

void PlaybackRoutingDialog::UpdateStatus(int rowIndex, int colIndex)
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
   mStatusText->SetLabel(wxString::Format(
      _("Track %d ('%s'), output %d"),
      rowIndex + 1, name, colIndex + 1));
}

void PlaybackRoutingDialog::OnResetRow(int rowIndex)
{
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return;
   auto &row = mRows[rowIndex];
   const auto channels = row.track ? row.track->NChannels() : size_t{0};
   auto identity = PlaybackOutputMask::Identity(
      static_cast<unsigned>(rowIndex), static_cast<unsigned>(channels));
   CheckboxesFromMask(row.checks, identity, mNumOutputChannels);
}

int PlaybackRoutingDialog::ApplyIntents()
{
   int changed = 0;
   for (auto &row : mRows) {
      if (!row.track)
         continue;
      // Preserve any bits set above the device width -- the user
      // cannot see them, but we don't want to silently clear them.
      auto newMask = row.originalStoredMask;
      for (size_t ch = 0; ch < mNumOutputChannels &&
                           ch < kMaxDisplayChannels; ++ch)
         newMask.clear(static_cast<unsigned>(ch));
      const auto visibleMask =
         MaskFromCheckboxes(row.checks, mNumOutputChannels);
      newMask.lo |= visibleMask.lo;
      newMask.hi |= visibleMask.hi;

      if (newMask != row.originalStoredMask) {
         row.track->SetPlaybackOutputMask(newMask);
         row.originalStoredMask = newMask;
         ++changed;
      }
   }
   return changed;
}

void PlaybackRoutingDialog::OnClose(wxCommandEvent &)
{
   ApplyIntents();
   EndModal(wxID_OK);
}
