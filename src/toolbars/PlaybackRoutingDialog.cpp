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
#include "ChannelRouting.h"     // kPlaybackRoutingSilentSentinel, ComputeChannelAssignments
#include "Project.h"
#include "ShuttleGui.h"
#include "Track.h"
#include "WaveTrack.h"

namespace {
// Bit 63 of the mask is reserved as the silent sentinel in
// lib-mixer/ChannelRouting.h.  Cap user-visible channels at 63 so a
// user can never accidentally build a checkbox state that collides
// with the sentinel.  Realistic devices are well under this limit.
constexpr size_t kMaxOutputChannels = 63;

//! Compute the mask the engine's auto routing would assign to a row,
//! given per-row track channel counts and (current) per-row masks.
//! The row's own mask is forced to 0 before calling
//! ComputeChannelAssignments so the result is what the engine would
//! do if the row were in Auto mode.
uint64_t AutoMaskFromAssignment(
   size_t rowIndex, size_t numOutputChannels,
   const std::vector<size_t>& counts)
{
   // All masks 0 -> everybody auto -> result shows pure auto layout
   // from the row's point of view.  Using other rows' current
   // explicit masks would be marginally more accurate, but the "pure
   // auto" answer is the predictable one for users: Reset restores
   // the layout you would have had on a fresh project.
   std::vector<uint64_t> masks(counts.size(), 0);
   const auto assignments = ComputeChannelAssignments(
      counts, masks, numOutputChannels);
   if (rowIndex >= assignments.size())
      return 0;

   const auto& a = assignments[rowIndex];
   const auto channels = counts[rowIndex];
   uint64_t mask = 0;

   if (a.outputChannel >= 0) {
      // Identity routing starting at outputChannel.  Multi-channel
      // tracks spread across consecutive outputs.
      const auto startCh = static_cast<size_t>(a.outputChannel);
      const auto n = channels > 0 ? channels : size_t{1};
      for (size_t i = 0; i < n; ++i) {
         const size_t ch = startCh + i;
         if (ch < numOutputChannels)
            mask |= (uint64_t(1) << ch);
      }
      return mask;
   }

   // Legacy (outputChannel == -1).  Matches RouteTrackSamples:
   // - mono source: duplicate to every output
   // - multi-channel source: identity from channel 0
   if (channels <= 1) {
      for (size_t ch = 0; ch < numOutputChannels; ++ch)
         mask |= (uint64_t(1) << ch);
   } else {
      const auto n = std::min<size_t>(channels, numOutputChannels);
      for (size_t i = 0; i < n; ++i)
         mask |= (uint64_t(1) << i);
   }
   return mask;
}

//! Read a row's checkboxes into a bitmask.
uint64_t MaskFromCheckboxes(const std::vector<wxCheckBox*>& checks)
{
   uint64_t mask = 0;
   for (size_t i = 0; i < checks.size() && i < kMaxOutputChannels; ++i)
      if (checks[i] && checks[i]->GetValue())
         mask |= (uint64_t(1) << i);
   return mask;
}

//! Set a row's checkboxes to match a bitmask.
void CheckboxesFromMask(
   const std::vector<wxCheckBox*>& checks, uint64_t mask)
{
   for (size_t i = 0; i < checks.size() && i < kMaxOutputChannels; ++i)
      if (checks[i])
         checks[i]->SetValue((mask & (uint64_t(1) << i)) != 0);
}

//! Map a stored mask value back to the initial dialog Intent.
//!   0                          -> Auto
//!   kPlaybackRoutingSilentSentinel -> Silent
//!   anything else              -> Explicit
PlaybackRoutingDialog::Intent IntentFromStoredMask(uint64_t stored)
{
   if (stored == 0)
      return PlaybackRoutingDialog::Intent::Auto;
   if (stored == kPlaybackRoutingSilentSentinel)
      return PlaybackRoutingDialog::Intent::Silent;
   return PlaybackRoutingDialog::Intent::Explicit;
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
   if (requested > static_cast<int>(kMaxOutputChannels))
      requested = static_cast<int>(kMaxOutputChannels);
   mNumOutputChannels = static_cast<size_t>(requested);

   BuildUI(focusedTrack);
}

PlaybackRoutingDialog::~PlaybackRoutingDialog() = default;

uint64_t PlaybackRoutingDialog::ComputeAutoMaskForRow(int rowIndex) const
{
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return 0;
   std::vector<size_t> counts;
   counts.reserve(mRows.size());
   for (const auto& row : mRows)
      counts.push_back(row.track ? row.track->NChannels() : 0);
   return AutoMaskFromAssignment(
      static_cast<size_t>(rowIndex), mNumOutputChannels, counts);
}

void PlaybackRoutingDialog::BuildUI(WaveTrack *focusedTrack)
{
   auto *outer = new wxBoxSizer(wxVERTICAL);

   // Header text explaining the semantics.  Empty row != Auto; this
   // line calls that out so the user isn't surprised.
   auto *header = new wxStaticText(this, wxID_ANY,
      XO("Each checkbox routes a track's audio to the corresponding "
         "device output channel.\n"
         "An empty row means the track is silenced (no playback). "
         "Use Reset to return a row to automatic routing.")
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

   // Seed rows with track refs and their stored masks so the
   // auto-routing pass can see channel counts.
   auto &tracks = TrackList::Get(mProject);
   for (auto pTrack : tracks.Any<WaveTrack>()) {
      TrackRow row;
      row.track = pTrack;
      row.originalStoredMask = pTrack->GetPlaybackOutputMask();
      row.intent = IntentFromStoredMask(row.originalStoredMask);
      mRows.push_back(std::move(row));
   }

   // Now build the widgets and set initial checkbox state.
   wxWindow *focusTarget = nullptr;
   for (size_t rowIndex = 0; rowIndex < mRows.size(); ++rowIndex) {
      auto &row = mRows[rowIndex];

      grid->Add(new wxStaticText(mMatrixPanel, wxID_ANY,
                                 row.track->GetName()),
                0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 2);

      // Each row gets mNumOutputChannels checkboxes.  Initial values
      // depend on intent:
      //   Auto     -> show the effective auto-routing bits
      //   Explicit -> show the stored mask bits
      //   Silent   -> nothing checked (stored sentinel has only bit
      //               63 set, which is outside our kMaxOutputChannels
      //               range, so this also gives "nothing checked"
      //               naturally)
      uint64_t displayMask = 0;
      switch (row.intent) {
         case Intent::Auto:
            displayMask = ComputeAutoMaskForRow(static_cast<int>(rowIndex));
            break;
         case Intent::Explicit:
            displayMask = row.originalStoredMask;
            break;
         case Intent::Silent:
            displayMask = 0;
            break;
      }

      for (size_t ch = 0; ch < mNumOutputChannels; ++ch) {
         auto *check = new wxCheckBox(mMatrixPanel, wxID_ANY,
                                      wxEmptyString);
         check->SetValue((displayMask & (uint64_t(1) << ch)) != 0);
         const int capturedIndex = static_cast<int>(rowIndex);
         check->Bind(wxEVT_CHECKBOX,
            [this, capturedIndex](wxCommandEvent &) {
               this->OnRowCheckboxToggled(capturedIndex);
            });
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

   // Single action: Close applies pending changes and dismisses.
   // The old Apply button was redundant -- it did the same thing as
   // Close without closing, which gave no visible feedback.
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
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return;
   auto &row = mRows[rowIndex];
   row.intent = Intent::Auto;
   const auto autoMask = ComputeAutoMaskForRow(rowIndex);
   CheckboxesFromMask(row.checks, autoMask);
}

void PlaybackRoutingDialog::OnRowCheckboxToggled(int rowIndex)
{
   if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= mRows.size())
      return;
   auto &row = mRows[rowIndex];
   const auto mask = MaskFromCheckboxes(row.checks);
   row.intent = (mask == 0) ? Intent::Silent : Intent::Explicit;
}

int PlaybackRoutingDialog::ApplyIntents()
{
   int changed = 0;
   for (auto &row : mRows) {
      if (!row.track)
         continue;
      uint64_t newStored = 0;
      switch (row.intent) {
         case Intent::Auto:
            newStored = 0;
            break;
         case Intent::Silent:
            newStored = kPlaybackRoutingSilentSentinel;
            break;
         case Intent::Explicit:
            // Recompute from live checkbox state (user may have been
            // clicking up to the moment of Close).
            newStored = MaskFromCheckboxes(row.checks);
            if (newStored == 0) {
               // Empty row while Intent says Explicit would be a
               // stale state after rapid user input; treat as Silent.
               newStored = kPlaybackRoutingSilentSentinel;
            }
            break;
      }
      if (newStored != row.originalStoredMask) {
         row.track->SetPlaybackOutputMask(newStored);
         row.originalStoredMask = newStored;
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
