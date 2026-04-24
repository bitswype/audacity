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

#include "AudioIOBase.h"        // for AudioIOPlaybackChannels
#include "PlaybackOutputMask.h"
#include "Project.h"
#include "ShuttleGui.h"
#include "Track.h"
#include "WaveTrack.h"

namespace {
//! Upper bound on columns the dialog will ever display.  Matches the
//! width of the underlying mask.  Realistic devices are far below
//! this, but the dialog will scroll horizontally if needed.
constexpr size_t kMaxDisplayChannels = kPlaybackOutputMaskBits;

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
   const std::vector<wxCheckBox*>& checks, const PlaybackOutputMask& mask,
   size_t numChannels)
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
                     wxDefaultPosition, wxSize(700, 480),
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

   BuildUI(focusedTrack);
}

PlaybackRoutingDialog::~PlaybackRoutingDialog() = default;

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

   mMatrixPanel = new wxScrolledWindow(this, wxID_ANY);
   mMatrixPanel->SetScrollRate(10, 10);

   // Column headers: "Track" + "1" "2" ... "N" + "Reset"
   const int cols = 1 + static_cast<int>(mNumOutputChannels) + 1;
   auto *grid = new wxFlexGridSizer(cols, 4, 4);
   grid->AddGrowableCol(0, 1);

   grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY, _("Track")),
             0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);
   for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
      auto *label = new wxStaticText(mMatrixPanel, wxID_ANY,
         wxString::Format("%zu", ch + 1),
         wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
      grid->Add(label, 0, wxALIGN_CENTER, 0);
   }
   grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY, _("Reset")),
             0, wxALIGN_CENTER, 0);

   auto &tracks = TrackList::Get(mProject);
   for (auto pTrack : tracks.Any<WaveTrack>()) {
      TrackRow row;
      row.track = pTrack;
      row.originalStoredMask = pTrack->GetPlaybackOutputMask();
      mRows.push_back(std::move(row));
   }

   wxWindow *focusTarget = nullptr;
   for (size_t rowIndex = 0; rowIndex < mRows.size(); ++rowIndex) {
      auto &row = mRows[rowIndex];

      grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY,
                                 row.track->GetName()),
                0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);

      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *check = new wxCheckBox(mMatrixPanel, wxID_ANY,
                                      wxEmptyString);
         check->SetValue(
            row.originalStoredMask.test(static_cast<unsigned>(ch)));
         row.checks.push_back(check);
         grid->Add(check, 0, wxALIGN_CENTER, 0);
      }

      auto *resetBtn = new wxButton(mMatrixPanel, wxID_ANY, _("Reset"),
         wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
      const int capturedIndex = static_cast<int>(rowIndex);
      resetBtn->Bind(wxEVT_BUTTON, [this, capturedIndex](wxCommandEvent &) {
         this->OnResetRow(capturedIndex);
      });
      grid->Add(resetBtn, 0, wxALIGN_CENTER, 0);

      if (row.track == focusedTrack)
         focusTarget = row.checks.empty() ? nullptr : row.checks[0];
   }

   mMatrixPanel->SetSizer(grid);
   mMatrixPanel->FitInside();
   outer->Add(mMatrixPanel, 1, wxEXPAND | wxALL, 6);

   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   auto *closeBtn = new wxButton(this, wxID_CLOSE, _("&Close"));
   closeBtn->Bind(wxEVT_BUTTON, &PlaybackRoutingDialog::OnClose, this);
   closeBtn->SetDefault();
   btnRow->AddStretchSpacer();
   btnRow->Add(closeBtn, 0, wxALL, 6);
   outer->Add(btnRow, 0, wxEXPAND);

   SetSizer(outer);

   if (focusTarget) {
      int x, y;
      focusTarget->GetPosition(&x, &y);
      mMatrixPanel->Scroll(0, y / 10);
      focusTarget->SetFocus();
   }
}

void PlaybackRoutingDialog::OnResetRow(int rowIndex)
{
   // Identity for this row: bits [rowIndex, rowIndex + channelCount)
   // clamped to the device width.  Matches the default the
   // PlaybackRoutingListener would have installed for a freshly added
   // track at this position.
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return;
   auto &row = mRows[rowIndex];
   const auto channels = row.track ? row.track->NChannels() : size_t{0};
   auto identity = PlaybackOutputMask::Identity(
      static_cast<unsigned>(rowIndex), static_cast<unsigned>(channels));
   // Don't show bits that are outside the device width.
   CheckboxesFromMask(row.checks, identity, mNumOutputChannels);
}

int PlaybackRoutingDialog::ApplyIntents()
{
   int changed = 0;
   for (auto &row : mRows) {
      if (!row.track)
         continue;
      // Preserve any bits set above the device width (the user cannot
      // see or toggle those, but their routing is still valid if the
      // device later widens).
      auto newMask = row.originalStoredMask;
      // Clear bits within the visible range, then set from checkboxes.
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
