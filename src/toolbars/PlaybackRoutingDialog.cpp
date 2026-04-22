/**********************************************************************

  Audacity: A Digital Audio Editor

  PlaybackRoutingDialog.cpp

**********************************************************************/

#include "PlaybackRoutingDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "AudioIOBase.h"         // for AudioIOPlaybackChannels
#include "Project.h"
#include "ShuttleGui.h"
#include "Track.h"
#include "WaveTrack.h"

namespace {
// Hard cap matching the uint64_t mask width.  Realistic devices are
// <= 32; we cap at 64 to fit in the per-track mask storage.
constexpr size_t kMaxOutputChannels = 64;
} // namespace

PlaybackRoutingDialog::PlaybackRoutingDialog(
   wxWindow *parent, AudacityProject &project, WaveTrack *focusedTrack)
   : wxDialogWrapper(parent, wxID_ANY,
                     XO("Playback Routing Matrix"),
                     wxDefaultPosition, wxSize(700, 480),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mProject(project)
{
   SetName();

   // Use the user-selected playback channel count for the matrix width.
   // Cap at kMaxOutputChannels in case someone sets a ridiculous value.
   auto requested = AudioIOPlaybackChannels.ReadWithDefault(2);
   if (requested < 1)
      requested = 1;
   if (requested > static_cast<int>(kMaxOutputChannels))
      requested = static_cast<int>(kMaxOutputChannels);
   mNumOutputChannels = static_cast<size_t>(requested);

   BuildUI(focusedTrack);
}

PlaybackRoutingDialog::~PlaybackRoutingDialog() = default;

void PlaybackRoutingDialog::BuildUI(WaveTrack *focusedTrack)
{
   auto *outer = new wxBoxSizer(wxVERTICAL);

   // Header text explaining what the matrix does.
   auto *header = new wxStaticText(this, wxID_ANY,
      XO("Each checkbox routes a track's audio to the corresponding "
         "device output channel.\n"
         "Leaving a row empty restores the default identity routing "
         "(track N -> output N).")
         .Translation());
   outer->Add(header, 0, wxEXPAND | wxALL, 8);

   // Scrollable area for the matrix itself.  Rows can be tall with
   // many tracks; columns can be wide with a 16- or 32-channel device.
   mMatrixPanel = new wxScrolledWindow(this, wxID_ANY);
   mMatrixPanel->SetScrollRate(10, 10);

   // Column headers: "Track" + "1" "2" ... "N" + "Reset"
   const int cols = 1 + static_cast<int>(mNumOutputChannels) + 1;
   auto *grid = new wxFlexGridSizer(cols, 4, 4);

   // Grow the "Track" column so it fills available width.
   grid->AddGrowableCol(0, 1);

   // -- Header row
   grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY, _("Track")),
             0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);
   for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
      auto *label = new wxStaticText(mMatrixPanel, wxID_ANY,
         wxString::Format("%zu", ch + 1),
         wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
      grid->Add(label, 0, wxALIGN_CENTER, 0);
   }
   // Reset-column header (empty label, column exists for alignment).
   grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY, _("Reset")),
             0, wxALIGN_CENTER, 0);

   // -- One row per wave track
   auto &tracks = TrackList::Get(mProject);
   wxWindow *focusTarget = nullptr;
   int rowIndex = 0;
   for (auto pTrack : tracks.Any<WaveTrack>()) {
      TrackRow row;
      row.track = pTrack;
      row.originalMask = pTrack->GetPlaybackOutputMask();

      grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY,
                                 pTrack->GetName()),
                0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);

      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *check = new wxCheckBox(mMatrixPanel, wxID_ANY, wxEmptyString);
         if (row.originalMask & (uint64_t(1) << ch))
            check->SetValue(true);
         row.checks.push_back(check);
         grid->Add(check, 0, wxALIGN_CENTER, 0);
      }

      // Per-row Reset button clears the row and restores identity
      // routing when Apply is clicked.
      auto *resetBtn = new wxButton(mMatrixPanel, wxID_ANY, _("Reset"),
         wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
      const int capturedIndex = rowIndex;
      resetBtn->Bind(wxEVT_BUTTON, [this, capturedIndex](wxCommandEvent &) {
         this->OnResetRow(capturedIndex);
      });
      grid->Add(resetBtn, 0, wxALIGN_CENTER, 0);

      if (pTrack == focusedTrack)
         focusTarget = row.checks.empty() ? nullptr : row.checks[0];

      mRows.push_back(std::move(row));
      ++rowIndex;
   }

   mMatrixPanel->SetSizer(grid);
   mMatrixPanel->FitInside();
   outer->Add(mMatrixPanel, 1, wxEXPAND | wxALL, 6);

   // -- Button row
   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   auto *applyBtn = new wxButton(this, wxID_APPLY, _("&Apply"));
   auto *closeBtn = new wxButton(this, wxID_CLOSE, _("&Close"));
   applyBtn->Bind(wxEVT_BUTTON, &PlaybackRoutingDialog::OnApply, this);
   closeBtn->Bind(wxEVT_BUTTON, &PlaybackRoutingDialog::OnClose, this);
   btnRow->AddStretchSpacer();
   btnRow->Add(applyBtn, 0, wxALL, 6);
   btnRow->Add(closeBtn, 0, wxALL, 6);
   outer->Add(btnRow, 0, wxEXPAND);

   SetSizer(outer);

   // Scroll to the focused row if requested.
   if (focusTarget) {
      int x, y;
      focusTarget->GetPosition(&x, &y);
      mMatrixPanel->Scroll(0, y / 10);
      focusTarget->SetFocus();
   }
}

void PlaybackRoutingDialog::OnResetRow(int rowIndex)
{
   if (rowIndex < 0 || rowIndex >= static_cast<int>(mRows.size()))
      return;
   for (auto *cb : mRows[rowIndex].checks)
      if (cb)
         cb->SetValue(false);
}

int PlaybackRoutingDialog::ApplyMasks()
{
   int changed = 0;
   for (auto &row : mRows) {
      if (!row.track)
         continue;
      uint64_t newMask = 0;
      for (size_t i = 0; i < row.checks.size() && i < kMaxOutputChannels; ++i)
         if (row.checks[i] && row.checks[i]->GetValue())
            newMask |= (uint64_t(1) << i);
      if (newMask != row.originalMask) {
         row.track->SetPlaybackOutputMask(newMask);
         row.originalMask = newMask;
         ++changed;
      }
   }
   return changed;
}

void PlaybackRoutingDialog::OnApply(wxCommandEvent &)
{
   ApplyMasks();
}

void PlaybackRoutingDialog::OnClose(wxCommandEvent &)
{
   // Apply on close so users don't lose their selections by accident.
   // (Esc or the window-close button dismisses without applying --
   // that goes through the default dialog cancel path.)
   ApplyMasks();
   EndModal(wxID_OK);
}
