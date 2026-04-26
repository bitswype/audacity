/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelTestToneDialog.cpp

**********************************************************************/

#include "ChannelTestToneDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/valnum.h>

#include "AudioIO.h"
#include "AudioIOBase.h"
#include "PlaybackRoutingDialog.h"
#include "Project.h"
#include "ProjectAudioIO.h"
#include "ProjectRate.h"

#include <algorithm>
#include <cstdlib>

namespace {
constexpr size_t kMaxDisplayChannels = kPlaybackOutputMaskBits;
//! Columns in the channel checkbox grid.  16 fits the typical
//! routing-matrix width on a 1280-wide display; the dialog scrolls
//! vertically when more channels are present (up to 128).
constexpr int kGridColumns = 16;
//! Slider in dB tens-of-a-dB so we can step in 0.1 dB.  Range
//! corresponds to -120 dBFS .. 0 dBFS.  The numeric text field
//! accepts values outside this range too.
constexpr int kSliderMinTenths = -1200;
constexpr int kSliderMaxTenths = 0;

double ParseDouble(const wxString& s, double fallback)
{
   double v = 0.0;
   if (!s.ToDouble(&v))
      return fallback;
   return v;
}
} // namespace

enum {
   kPlayId = 1000,
   kStopId,
   kCycleId,
   kSelectAllId,
   kClearId,
   kFreqTextId,
   kLevelTextId,
   kLevelSliderId,
   kToneTypeId,
   kModeDirectId,
   kModeMatrixId,
   kDwellTextId,
   kPollTimerId,
   kCycleTimerId,
   kOpenMatrixId,
};

BEGIN_EVENT_TABLE(ChannelTestToneDialog, wxDialogWrapper)
   EVT_BUTTON(kPlayId, ChannelTestToneDialog::OnPlay)
   EVT_BUTTON(kStopId, ChannelTestToneDialog::OnStop)
   EVT_BUTTON(kCycleId, ChannelTestToneDialog::OnCycle)
   EVT_BUTTON(kSelectAllId, ChannelTestToneDialog::OnSelectAll)
   EVT_BUTTON(kClearId, ChannelTestToneDialog::OnClear)
   EVT_BUTTON(kOpenMatrixId, ChannelTestToneDialog::OnOpenMatrix)
   EVT_BUTTON(wxID_CLOSE, ChannelTestToneDialog::OnCloseButton)
   EVT_CLOSE(ChannelTestToneDialog::OnClose)
   EVT_TEXT(kFreqTextId, ChannelTestToneDialog::OnFrequencyText)
   EVT_TEXT(kLevelTextId, ChannelTestToneDialog::OnLevelText)
   EVT_SLIDER(kLevelSliderId, ChannelTestToneDialog::OnLevelSlider)
   EVT_CHOICE(kToneTypeId, ChannelTestToneDialog::OnToneType)
   EVT_RADIOBUTTON(kModeDirectId, ChannelTestToneDialog::OnMode)
   EVT_RADIOBUTTON(kModeMatrixId, ChannelTestToneDialog::OnMode)
   EVT_TIMER(kPollTimerId, ChannelTestToneDialog::OnPollTimer)
   EVT_TIMER(kCycleTimerId, ChannelTestToneDialog::OnCycleTimer)
END_EVENT_TABLE()

ChannelTestToneDialog::ChannelTestToneDialog(
   wxWindow* parent, AudacityProject& project)
   : wxDialogWrapper(parent, wxID_ANY,
                     XO("Channel Test Tone"),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mProject(project)
{
   SetName();

   const auto requested = AudioIOPlaybackChannels.ReadWithDefault(2);
   mNumDeviceChannels = static_cast<size_t>(
      std::clamp<int>(requested, 1,
         static_cast<int>(kMaxDisplayChannels)));

   BuildUI();

   mPollTimer = std::make_unique<wxTimer>(this, kPollTimerId);
   mPollTimer->Start(250, wxTIMER_CONTINUOUS);
   mCycleTimer = std::make_unique<wxTimer>(this, kCycleTimerId);

   RefreshControlState();
}

ChannelTestToneDialog::~ChannelTestToneDialog()
{
   if (mPollTimer)
      mPollTimer->Stop();
   if (mCycleTimer)
      mCycleTimer->Stop();
   // If the dialog is destroyed while a tone is playing, stop it.
   auto* gAudioIO = AudioIO::Get();
   if (gAudioIO && gAudioIO->IsTestToneActive())
      gAudioIO->StopTestTone();
}

void ChannelTestToneDialog::BuildUI()
{
   auto* outer = new wxBoxSizer(wxVERTICAL);

   // Mode radio buttons + access to the Playback Routing Matrix.
   //
   // Both modes use the same checkbox grid below to define the mask.
   // The user-facing difference is which code path the audio engine
   // runs the tone through:
   //
   //   Direct: tone -> outputBuffer[bit] for each set bit (no routing
   //     engine).  Useful to confirm "speaker B is wired to physical
   //     output A" independent of any routing logic.
   //   Matrix: tone -> RouteTrackSamples(mask) -> outputBuffer
   //     (production engine).  Useful to confirm the routing engine
   //     itself is correctly walking the mask -- if Direct works on
   //     the same channel and Matrix doesn't, the bug is in
   //     RouteTrackSamples or its mask interpretation.
   //
   // The "Open Routing Matrix..." button surfaces the per-track
   // routing dialog so the user can configure / inspect the saved
   // routing for real tracks alongside the test tone, without
   // closing this dialog.
   {
      auto* box = new wxStaticBoxSizer(wxVERTICAL, this, _("Test mode"));
      mDirectRadio = new wxRadioButton(this, kModeDirectId,
         _("Direct hardware test -- bypass routing engine "
           "(verify physical wiring)"),
         wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      mMatrixRadio = new wxRadioButton(this, kModeMatrixId,
         _("Routing matrix test -- run through RouteTrackSamples "
           "(verify routing engine for the same mask)"));
      mDirectRadio->SetValue(true);
      box->Add(mDirectRadio, 0, wxALL, 4);
      box->Add(mMatrixRadio, 0, wxALL, 4);

      auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
      // No custom foreground: GRAYTEXT inside a wxStaticBoxSizer can
      // resolve to dark-on-dark under GTK dark themes.  Use the
      // theme's default text colour and lean on italics for the
      // visual hierarchy, which works in both light and dark.
      auto* hint = new wxStaticText(this, wxID_ANY,
         _("Tip: same checkbox grid drives both modes; only the audio "
           "code path differs."));
      auto hintFont = hint->GetFont();
      hintFont.MakeItalic();
      hint->SetFont(hintFont);
      btnRow->Add(hint, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      auto* openMatrixBtn =
         new wxButton(this, kOpenMatrixId, _("Open Routing &Matrix..."));
      openMatrixBtn->SetToolTip(
         _("Open the per-track Playback Routing Matrix in a separate "
           "dialog.  Useful for configuring track routing while you "
           "test it here."));
      btnRow->Add(openMatrixBtn, 0);
      box->Add(btnRow, 0, wxALL | wxEXPAND, 4);

      outer->Add(box, 0, wxALL | wxEXPAND, 6);
   }

   // Tone type + frequency
   {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(this, wxID_ANY, _("Tone:")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      wxArrayString toneTypes;
      toneTypes.Add(_("Sine"));
      toneTypes.Add(_("Pink noise"));
      toneTypes.Add(_("White noise"));
      mToneTypeChoice = new wxChoice(this, kToneTypeId,
         wxDefaultPosition, wxDefaultSize, toneTypes);
      mToneTypeChoice->SetSelection(0);
      row->Add(mToneTypeChoice, 0, wxRIGHT, 12);

      row->Add(new wxStaticText(this, wxID_ANY, _("Frequency:")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      // Create empty + ChangeValue() afterwards.  The ctor's value
      // argument fires wxEVT_TEXT synchronously, which would dispatch
      // our OnFrequencyText handler before the mFreqText assignment
      // landed -- the handler would dereference a null member and
      // crash the dialog on first open.  ChangeValue() suppresses the
      // event.  Same pattern for mLevelText below.
      mFreqText = new wxTextCtrl(this, kFreqTextId, wxEmptyString,
         wxDefaultPosition, wxSize(80, -1));
      mFreqText->ChangeValue(wxT("1000.0"));
      row->Add(mFreqText, 0, wxRIGHT, 4);
      row->Add(new wxStaticText(this, wxID_ANY, _("Hz")),
         0, wxALIGN_CENTER_VERTICAL);
      outer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
   }

   // Level
   {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(this, wxID_ANY, _("Level:")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      mLevelText = new wxTextCtrl(this, kLevelTextId, wxEmptyString,
         wxDefaultPosition, wxSize(72, -1));
      mLevelText->ChangeValue(wxT("-20.0"));
      row->Add(mLevelText, 0, wxRIGHT, 4);
      row->Add(new wxStaticText(this, wxID_ANY, _("dBFS")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      mLevelSlider = new wxSlider(this, kLevelSliderId,
         /*value*/ -200, kSliderMinTenths, kSliderMaxTenths,
         wxDefaultPosition, wxSize(220, -1));
      row->Add(mLevelSlider, 1, wxALIGN_CENTER_VERTICAL);
      outer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
   }

   // Channel grid (scrollable).  Lays out kGridColumns wide; rows
   // grow to cover all kMaxDisplayChannels.
   {
      auto* label = new wxStaticText(this, wxID_ANY,
         wxString::Format(_("Target output channels (1 - %u, %zu reachable on this device):"),
            static_cast<unsigned>(kMaxDisplayChannels),
            mNumDeviceChannels));
      outer->Add(label, 0, wxLEFT | wxRIGHT, 6);

      // Both scroll bars enabled.  Horizontal is needed because the
      // grid is 16 columns wide -- on smaller displays (or when the
      // user shrinks the dialog) the right-hand columns would
      // otherwise vanish off the edge with no way to reach them.
      mGridScroll = new wxScrolledWindow(this, wxID_ANY,
         wxDefaultPosition, wxSize(880, 240),
         wxBORDER_SIMPLE | wxHSCROLL | wxVSCROLL);
      auto* grid = new wxFlexGridSizer(kGridColumns, 4, 4);
      mChannelChecks.assign(kMaxDisplayChannels, nullptr);
      for (size_t i = 0; i < kMaxDisplayChannels; ++i) {
         const wxString label = wxString::Format(wxT("%zu"), i + 1);
         auto* cb = new wxCheckBox(mGridScroll, wxID_ANY, label);
         if (i >= mNumDeviceChannels) {
            cb->SetForegroundColour(
               wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            cb->SetToolTip(
               _("This channel is past the device's reachable output count; "
                 "the tone will be silently dropped if selected."));
         }
         grid->Add(cb, 0, wxALL, 2);
         mChannelChecks[i] = cb;
      }
      mGridScroll->SetSizer(grid);
      mGridScroll->FitInside();
      mGridScroll->SetScrollRate(16, 16);
      outer->Add(mGridScroll, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
   }

   // Select all / clear
   {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxButton(this, kSelectAllId, _("Select &all")),
         0, wxRIGHT, 4);
      row->Add(new wxButton(this, kClearId, _("&Clear")),
         0, wxRIGHT, 4);
      outer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
   }

   // Status line
   mStatusText = new wxStaticText(this, wxID_ANY, _("Idle."));
   outer->Add(mStatusText, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

   // Bottom buttons
   {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(this, wxID_ANY, _("Cycle dwell:")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      mDwellText = new wxTextCtrl(this, kDwellTextId, wxEmptyString,
         wxDefaultPosition, wxSize(56, -1));
      mDwellText->ChangeValue(wxT("2.0"));
      row->Add(mDwellText, 0, wxRIGHT, 4);
      row->Add(new wxStaticText(this, wxID_ANY, _("s/ch")),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
      mCycleBtn = new wxButton(this, kCycleId, _("&Cycle selected"));
      mCycleBtn->SetToolTip(
         _("Walk through the checked channels one at a time, "
           "playing the configured tone on each for the dwell "
           "time before advancing.  Click again to stop."));
      row->Add(mCycleBtn, 0, wxRIGHT, 12);
      row->AddStretchSpacer();
      mPlayBtn = new wxButton(this, kPlayId, _("&Play"));
      mStopBtn = new wxButton(this, kStopId, _("&Stop"));
      row->Add(mPlayBtn, 0, wxRIGHT, 4);
      row->Add(mStopBtn, 0, wxRIGHT, 12);
      row->Add(new wxButton(this, wxID_CLOSE, _("Close")), 0);
      outer->Add(row, 0, wxALL | wxEXPAND, 6);
   }

   SetSizerAndFit(outer);
   // Default size big enough to show all 16 grid columns natively on
   // typical 1920x1080 displays without horizontal scroll, while
   // staying small enough to fit a 1366x768 laptop.  The scroll bars
   // are still present so smaller screens / shrunk dialogs can reach
   // the right-hand columns.
   SetMinSize(wxSize(720, 540));
   SetSize(wxSize(960, 620));
}

TestToneRequest ChannelTestToneDialog::MakeRequest() const
{
   TestToneRequest r;
   r.mode = (mMatrixRadio && mMatrixRadio->GetValue())
      ? TestToneRequest::Mode::ThroughMatrix
      : TestToneRequest::Mode::DirectHW;

   const int sel = mToneTypeChoice ? mToneTypeChoice->GetSelection() : 0;
   switch (sel) {
   case 1: r.toneType = TestToneGenerator::Type::Pink; break;
   case 2: r.toneType = TestToneGenerator::Type::White; break;
   default: r.toneType = TestToneGenerator::Type::Sine; break;
   }

   // Wrap the wxT literals in wxString to make the conditional
   // expression's type unambiguous to MSVC -- GCC tolerates the
   // implicit conversion from wxString to const wchar_t* but MSVC
   // sees both branches and refuses to pick.
   r.frequencyHz = ParseDouble(
      mFreqText ? mFreqText->GetValue() : wxString(wxT("1000.0")),
      1000.0);
   r.levelDb = ParseDouble(
      mLevelText ? mLevelText->GetValue() : wxString(wxT("-20.0")),
      -20.0);

   PlaybackOutputMask mask;
   for (size_t i = 0; i < mChannelChecks.size(); ++i) {
      if (mChannelChecks[i] && mChannelChecks[i]->GetValue())
         mask.set(static_cast<unsigned>(i));
   }
   r.mask = mask;
   return r;
}

void ChannelTestToneDialog::PushParamsIfActive()
{
   auto* gAudioIO = AudioIO::Get();
   if (!gAudioIO || !gAudioIO->IsTestToneActive())
      return;
   const auto req = MakeRequest();
   gAudioIO->UpdateTestTone(req);
   mLast = req;
}

void ChannelTestToneDialog::RefreshControlState()
{
   auto* gAudioIO = AudioIO::Get();
   const bool active =
      gAudioIO ? gAudioIO->IsTestToneActive() : false;
   const bool busy =
      gAudioIO ? gAudioIO->IsBusy() : false;
   if (mPlayBtn)
      mPlayBtn->Enable(!active && !busy);
   if (mStopBtn)
      mStopBtn->Enable(active);
   if (mCycleBtn)
      mCycleBtn->SetLabel(mCycling
         ? _("Stop &cycling")
         : _("&Cycle selected"));

   if (mCycling && active && !mCycleBits.empty()) {
      const unsigned cur = mCycleBits[mCycleIndex] + 1;
      mStatusText->SetLabel(wxString::Format(
         _("Cycling: channel %u (%zu of %zu)"),
         cur, mCycleIndex + 1, mCycleBits.size()));
   } else if (active) {
      // Build "Playing on channels: 5, 10" up to a sensible truncation.
      wxString channelsStr;
      const auto req = MakeRequest();
      int shown = 0;
      for (unsigned i = 0; i < kPlaybackOutputMaskBits; ++i) {
         if (!req.mask.test(i)) continue;
         if (shown >= 12) {
            channelsStr += wxT(", ...");
            break;
         }
         if (!channelsStr.empty()) channelsStr += wxT(", ");
         channelsStr += wxString::Format(wxT("%u"), i + 1);
         ++shown;
      }
      if (channelsStr.empty())
         mStatusText->SetLabel(_("Playing.  No channels selected -- silent."));
      else
         mStatusText->SetLabel(
            wxString::Format(_("Playing on channels: %s"), channelsStr));
   } else if (busy) {
      mStatusText->SetLabel(
         _("Audio system busy (other playback or recording in progress)."));
   } else {
      mStatusText->SetLabel(_("Idle."));
   }
}

void ChannelTestToneDialog::OnPlay(wxCommandEvent&)
{
   auto* gAudioIO = AudioIO::Get();
   if (!gAudioIO) return;
   // A direct Play takes precedence over an in-flight cycle.  Stop
   // the cycle (timer + state) before opening the stream, otherwise
   // the dwell timer would keep rewriting the mask underneath us.
   if (mCycling)
      StopCycle();
   if (gAudioIO->IsBusy()) {
      RefreshControlState();
      return;
   }
   const auto req = MakeRequest();
   AudioIOStartStreamOptions options(
      mProject.shared_from_this(),
      ProjectRate::Get(mProject).GetRate());
   if (!gAudioIO->StartTestTone(req, options)) {
      mStatusText->SetLabel(_("Failed to open the audio device."));
      return;
   }
   mLast = req;
   RefreshControlState();
}

void ChannelTestToneDialog::OnStop(wxCommandEvent&)
{
   auto* gAudioIO = AudioIO::Get();
   if (!gAudioIO) return;
   if (mCycling)
      StopCycle();
   gAudioIO->StopTestTone();
   RefreshControlState();
}

void ChannelTestToneDialog::OnCycle(wxCommandEvent&)
{
   if (mCycling)
      StopCycle();
   else
      StartCycle();
   RefreshControlState();
}

void ChannelTestToneDialog::OnOpenMatrix(wxCommandEvent&)
{
   // The routing matrix dialog mutates per-track masks via an Apply
   // step.  In matrix mode the test tone uses whatever mask is in
   // our checkbox grid -- not a project track's mask -- so changes
   // there do NOT affect the running tone.  Stop the tone first
   // anyway, since the routing dialog is modal and most users want
   // a quiet test bench while wiring up real tracks.
   auto* gAudioIO = AudioIO::Get();
   if (gAudioIO && gAudioIO->IsTestToneActive()) {
      if (mCycling) StopCycle();
      gAudioIO->StopTestTone();
   }
   PlaybackRoutingDialog dlg(this, mProject);
   dlg.ShowModal();
   RefreshControlState();
}

void ChannelTestToneDialog::OnCycleTimer(wxTimerEvent&)
{
   if (!mCycling || mCycleBits.empty())
      return;
   mCycleIndex = (mCycleIndex + 1) % mCycleBits.size();
   ApplyCycleStep();
   RefreshControlState();
}

void ChannelTestToneDialog::StartCycle()
{
   // Snapshot the currently checked channels so the cycle order is
   // stable even if the user touches the grid mid-cycle.  Bits past
   // the device's reachable count are filtered out so we don't dwell
   // silently on a bit that produces no sound.
   mCycleBits.clear();
   for (size_t i = 0; i < mChannelChecks.size() && i < mNumDeviceChannels;
        ++i)
   {
      if (mChannelChecks[i] && mChannelChecks[i]->GetValue())
         mCycleBits.push_back(static_cast<unsigned>(i));
   }
   if (mCycleBits.empty()) {
      mStatusText->SetLabel(
         _("Select one or more reachable channels to cycle."));
      return;
   }
   mCycleIndex = 0;
   mCycling = true;
   ApplyCycleStep();

   // Parse dwell, clamp to a sensible 0.05 - 60 s range so a
   // mistyped value doesn't turn the cycle into "play forever on
   // channel 1" or "rip-tear at 1ms/ch".
   double dwellSec = 2.0;
   if (mDwellText) {
      dwellSec = ParseDouble(mDwellText->GetValue(), 2.0);
      dwellSec = std::clamp(dwellSec, 0.05, 60.0);
   }
   const int dwellMs = static_cast<int>(dwellSec * 1000.0);
   if (mCycleTimer)
      mCycleTimer->Start(dwellMs, wxTIMER_CONTINUOUS);
}

void ChannelTestToneDialog::StopCycle()
{
   mCycling = false;
   if (mCycleTimer)
      mCycleTimer->Stop();
   mCycleBits.clear();
   mCycleIndex = 0;
}

void ChannelTestToneDialog::ApplyCycleStep()
{
   if (mCycleBits.empty())
      return;
   auto* gAudioIO = AudioIO::Get();
   if (!gAudioIO)
      return;

   // Build a request that uses only the current cycle channel.
   TestToneRequest req = MakeRequest();
   PlaybackOutputMask single;
   single.set(mCycleBits[mCycleIndex]);
   req.mask = single;

   if (gAudioIO->IsTestToneActive()) {
      gAudioIO->UpdateTestTone(req);
   } else if (!gAudioIO->IsBusy()) {
      AudioIOStartStreamOptions options(
         mProject.shared_from_this(),
         ProjectRate::Get(mProject).GetRate());
      if (!gAudioIO->StartTestTone(req, options))
         mStatusText->SetLabel(_("Failed to open the audio device."));
   }
   mLast = req;
}

void ChannelTestToneDialog::OnSelectAll(wxCommandEvent&)
{
   for (size_t i = 0; i < mChannelChecks.size() && i < mNumDeviceChannels; ++i)
      if (mChannelChecks[i])
         mChannelChecks[i]->SetValue(true);
   PushParamsIfActive();
   RefreshControlState();
}

void ChannelTestToneDialog::OnClear(wxCommandEvent&)
{
   for (auto* cb : mChannelChecks)
      if (cb) cb->SetValue(false);
   PushParamsIfActive();
   RefreshControlState();
}

void ChannelTestToneDialog::OnCloseButton(wxCommandEvent&)
{
   Close();
}

void ChannelTestToneDialog::OnClose(wxCloseEvent& event)
{
   if (mCycling) StopCycle();
   auto* gAudioIO = AudioIO::Get();
   if (gAudioIO && gAudioIO->IsTestToneActive())
      gAudioIO->StopTestTone();
   if (mPollTimer) mPollTimer->Stop();
   event.Skip();
}

void ChannelTestToneDialog::OnFrequencyText(wxCommandEvent&)
{
   PushParamsIfActive();
}

void ChannelTestToneDialog::OnLevelText(wxCommandEvent&)
{
   const double v = ParseDouble(mLevelText->GetValue(), -20.0);
   if (mLevelSlider) {
      const int tenths =
         std::clamp(static_cast<int>(v * 10.0),
            kSliderMinTenths, kSliderMaxTenths);
      // Avoid feedback loops -- only push if actually different.
      if (mLevelSlider->GetValue() != tenths)
         mLevelSlider->SetValue(tenths);
   }
   PushParamsIfActive();
}

void ChannelTestToneDialog::OnLevelSlider(wxCommandEvent&)
{
   const int tenths = mLevelSlider->GetValue();
   const double v = static_cast<double>(tenths) / 10.0;
   if (mLevelText) {
      const wxString newText = wxString::Format(wxT("%.1f"), v);
      if (mLevelText->GetValue() != newText)
         mLevelText->ChangeValue(newText);
   }
   PushParamsIfActive();
}

void ChannelTestToneDialog::OnToneType(wxCommandEvent&)
{
   PushParamsIfActive();
}

void ChannelTestToneDialog::OnMode(wxCommandEvent&)
{
   PushParamsIfActive();
}

void ChannelTestToneDialog::OnPollTimer(wxTimerEvent&)
{
   RefreshControlState();
}
