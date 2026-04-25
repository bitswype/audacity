/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelTestToneDialog.h

  bitswype fork: dialog for the Channel Test Tone feature.  Lets the
  user pick a tone (sine / pink / white), set frequency and level,
  choose target output channels, and toggle between two modes:

   - Direct hardware test: tone is written straight to physical output
     channels, bypassing the routing matrix.  Verifies "channel A is
     wired to speaker B".
   - Routing matrix test: tone runs through the production
     RouteTrackSamples pipeline using the chosen mask.  Verifies the
     routing engine produces the expected outputs for that mask.

  Up to 128 output channels (kPlaybackOutputMaskBits) -- the channel
  grid scrolls when the device or mask reaches further than the
  dialog's natural width.  The dialog is non-modal so the user can
  watch the meter and tweak parameters live; closing the dialog
  stops the tone.

**********************************************************************/
#pragma once

#include "PlaybackOutputMask.h"
#include "TestToneGenerator.h"
#include "wxPanelWrapper.h"

#include <vector>

class AudacityProject;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxRadioButton;
class wxScrolledWindow;
class wxSlider;
class wxStaticText;
class wxTextCtrl;
class wxTimer;
class wxTimerEvent;

class ChannelTestToneDialog final : public wxDialogWrapper
{
public:
   ChannelTestToneDialog(wxWindow* parent, AudacityProject& project);
   ~ChannelTestToneDialog() override;

private:
   void BuildUI();
   //! Snapshot the current control values into a TestToneRequest.
   TestToneRequest MakeRequest() const;
   //! Push current parameters to AudioIO if the tone is already active.
   void PushParamsIfActive();
   //! Refresh control enable state and the status line.
   void RefreshControlState();

   void OnPlay(wxCommandEvent&);
   void OnStop(wxCommandEvent&);
   void OnSelectAll(wxCommandEvent&);
   void OnClear(wxCommandEvent&);
   void OnCloseButton(wxCommandEvent&);
   void OnClose(wxCloseEvent&);
   void OnFrequencyText(wxCommandEvent&);
   void OnLevelText(wxCommandEvent&);
   void OnLevelSlider(wxCommandEvent&);
   void OnToneType(wxCommandEvent&);
   void OnMode(wxCommandEvent&);
   void OnPollTimer(wxTimerEvent&);

   AudacityProject& mProject;

   //! Number of physical device output channels (used as the
   //! reachable count even though the grid extends to 128).
   size_t mNumDeviceChannels = 2;

   wxRadioButton* mDirectRadio = nullptr;
   wxRadioButton* mMatrixRadio = nullptr;
   wxChoice* mToneTypeChoice = nullptr;
   wxTextCtrl* mFreqText = nullptr;
   wxTextCtrl* mLevelText = nullptr;
   wxSlider* mLevelSlider = nullptr;
   wxScrolledWindow* mGridScroll = nullptr;
   //! One per displayed output channel (size == kPlaybackOutputMaskBits).
   std::vector<wxCheckBox*> mChannelChecks;
   wxButton* mPlayBtn = nullptr;
   wxButton* mStopBtn = nullptr;
   wxStaticText* mStatusText = nullptr;
   //! 250ms poll timer to refresh status line when AudioIO state
   //! changes outside of our control (another stream stops, etc.).
   std::unique_ptr<wxTimer> mPollTimer;

   //! Cached last-applied request.  Lets us avoid hammering AudioIO
   //! with no-op updates on every keystroke / slider tick.
   TestToneRequest mLast;

   wxDECLARE_EVENT_TABLE();
};
