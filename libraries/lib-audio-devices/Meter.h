/*!********************************************************************

Audacity: A Digital Audio Editor

@file Meter.h

Paul Licameli split from MeterPanelBase.h

**********************************************************************/

#ifndef __AUDACITY_METER__
#define __AUDACITY_METER__

//! AudioIO uses this to send sample buffers for real-time display updates
class AUDIO_DEVICES_API Meter /* not final */
{
public:
   virtual ~Meter();

   virtual void Clear() = 0;
   virtual void Reset(double sampleRate, bool resetClipping) = 0;
   virtual void UpdateDisplay(unsigned numChannels,
                      unsigned long numFrames, const float *sampleData) = 0;
   virtual bool IsMeterDisabled() const = 0;
   virtual float GetMaxPeak() const = 0;
   virtual bool IsClipping() const = 0;
   virtual int GetDBRange() const = 0;

   //! bitswype fork: tell the meter how many channels the audio
   //! stream will deliver.  AudioIO calls this from StartStream so
   //! the meter sets up its bar count and layout before the first
   //! UpdateDisplay arrives, avoiding a stereo-then-multichannel
   //! visual snap when a many-channel device starts up.
   //! Default no-op so non-MeterPanel implementations of this
   //! interface (e.g. test mocks) don't need to care.
   virtual void SetNumChannels(unsigned /*numChannels*/) {}
};

#endif
