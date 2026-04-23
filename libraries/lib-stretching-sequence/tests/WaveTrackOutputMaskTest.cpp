/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrackOutputMaskTest.cpp

  Tests for WaveTrack::Get/SetPlaybackOutputMask and the XML serialization
  of the "outputmask" attribute.

  Placed in lib-stretching-sequence/tests because that test target already
  links lib-wave-track and has the TestWaveTrackMaker helper; spinning up
  a new lib-wave-track/tests/ subdir for just this would duplicate the
  mocked-audio / mocked-prefs plumbing for no benefit.

**********************************************************************/
#include "MockSampleBlockFactory.h"
#include "TestWaveTrackMaker.h"

#include "WaveTrack.h"
#include "XMLFileReader.h"
#include "XMLWriter.h"

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

namespace
{
constexpr int kSampleRate = 44100;

//! Shared maker so tests don't each have to plumb the factory.
//! The static lifetime matches what other tests in this directory do.
const auto gSampleBlockFactory = std::make_shared<MockSampleBlockFactory>();
TestWaveTrackMaker gTrackMaker { kSampleRate, gSampleBlockFactory };

//! Serialize a WaveTrack to an XML string.
wxString WriteWaveTrackToString(const WaveTrack& track)
{
   XMLStringWriter writer;
   track.WriteXML(writer);
   return writer;
}
} // namespace

TEST_CASE(
   "WaveTrack: default playback output mask is 0",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask() == 0);
}

TEST_CASE(
   "WaveTrack: set then get round-trips a non-zero mask",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask(0x12345678ull);
   REQUIRE(track->GetPlaybackOutputMask() == 0x12345678ull);
}

TEST_CASE(
   "WaveTrack: clearing the mask (set to 0) round-trips",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask(0xCAFEull);
   REQUIRE(track->GetPlaybackOutputMask() == 0xCAFEull);
   track->SetPlaybackOutputMask(0);
   REQUIRE(track->GetPlaybackOutputMask() == 0);
}

TEST_CASE(
   "WaveTrack: multiple sets -- last writer wins",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask(0x1);
   track->SetPlaybackOutputMask(0x2);
   track->SetPlaybackOutputMask(0x4);
   REQUIRE(track->GetPlaybackOutputMask() == 0x4);
}

TEST_CASE(
   "WaveTrack: large mask (bit 63) round-trips",
   "[WaveTrack][OutputMask]")
{
   const uint64_t hugeMask = 1ull << 63;
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask(hugeMask);
   REQUIRE(track->GetPlaybackOutputMask() == hugeMask);
}

TEST_CASE(
   "WaveTrack: mask survives Clone (WaveTrackData copy constructor)",
   "[WaveTrack][OutputMask]")
{
   // WaveTrackData is ClientData::Cloneable; WaveTrack::Clone copies the
   // attachment, which goes through WaveTrackData's copy constructor at
   // WaveTrack.cpp:263 (SetPlaybackOutputMask(other.GetPlaybackOutputMask())).
   // This is the only non-default code path since mPlaybackOutputMask is
   // std::atomic<uint64_t> and therefore non-copyable by default.
   const auto original = gTrackMaker.Track(WaveClipHolders {});
   original->SetPlaybackOutputMask(0xABCDEFull);

   const auto copy = std::static_pointer_cast<WaveTrack>(original->Duplicate());
   REQUIRE(copy != nullptr);
   REQUIRE(copy->GetPlaybackOutputMask() == 0xABCDEFull);
}

TEST_CASE(
   "WaveTrack XML: mask of 0 is NOT written (backward-compat with upstream)",
   "[WaveTrack][OutputMask][XML]")
{
   // The fork only writes the attribute when the mask is non-default, so
   // projects with no explicit routing stay byte-compatible with upstream
   // Audacity.
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask() == 0);

   const auto xml = WriteWaveTrackToString(*track);
   // Attribute must not appear anywhere in the serialized XML.
   REQUIRE(xml.Find("outputmask") == wxNOT_FOUND);
}

TEST_CASE(
   "WaveTrack XML: non-zero mask writes the outputmask attribute",
   "[WaveTrack][OutputMask][XML]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask(0xCAFEull);

   const auto xml = WriteWaveTrackToString(*track);
   // Attribute must appear. 0xCAFE == 51966; XMLWriter writes size_t as
   // decimal, so look for the attr/value combined to avoid false positives
   // from other attributes that happen to contain the digits "51966".
   REQUIRE(xml.Find("outputmask=\"51966\"") != wxNOT_FOUND);
}

TEST_CASE(
   "WaveTrack XML: reading a tag with outputmask sets the mask on the track",
   "[WaveTrack][OutputMask][XML]")
{
   // Drive HandleXMLTag directly with a synthetic attribute list. This is
   // the same entry point that XMLFileReader invokes for each parsed tag,
   // minus the expat plumbing.
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask() == 0);

   // 0xC0FFEE == 12648430
   const unsigned long long kMaskValue = 0xC0FFEEull;
   AttributesList attrs {
      { std::string_view { "outputmask" },
        XMLAttributeValueView { kMaskValue } }
   };
   REQUIRE(track->HandleXMLTag(WaveTrack::WaveTrack_tag, attrs));
   REQUIRE(track->GetPlaybackOutputMask() == kMaskValue);
}

TEST_CASE(
   "WaveTrack XML: reading a tag WITHOUT outputmask leaves the mask at 0",
   "[WaveTrack][OutputMask][XML]")
{
   // Opening a project saved by upstream (or this fork with no routing set)
   // must not change the default mask.
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask() == 0);

   AttributesList attrs {}; // no attributes at all
   REQUIRE(track->HandleXMLTag(WaveTrack::WaveTrack_tag, attrs));
   REQUIRE(track->GetPlaybackOutputMask() == 0);
}

TEST_CASE(
   "WaveTrack XML: full round-trip via XMLFileReader preserves the mask",
   "[WaveTrack][OutputMask][XML]")
{
   // Full round-trip: write XML, parse it back, verify mask is restored.
   const auto source = gTrackMaker.Track(WaveClipHolders {});
   source->SetPlaybackOutputMask(0xFEEDFACEull);

   const auto xml = WriteWaveTrackToString(*source);
   REQUIRE(xml.Find("outputmask") != wxNOT_FOUND);

   const auto target = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(target->GetPlaybackOutputMask() == 0);

   XMLFileReader reader;
   const auto ok = reader.ParseString(target.get(), xml);
   // ParseString may return false if the child clips are incomplete, but
   // the <wavetrack> tag itself and its attributes should have been handled
   // before any such failure. Either way, the mask must be set.
   (void)ok;
   REQUIRE(target->GetPlaybackOutputMask() == 0xFEEDFACEull);
}

TEST_CASE("PlaybackOutputMask tolerates concurrent reader and writer",
          "[WaveTrack][OutputMask][Threading]")
{
   // The mask is std::atomic<uint64_t> so that the audio thread can read
   // it while the UI thread writes it without a lock.  On 32-bit systems
   // a non-atomic uint64_t load/store is two separate 32-bit operations
   // and the reader could observe a torn value (high half from the old
   // value, low half from the new).  This test catches a future refactor
   // that accidentally drops the atomic wrapper.
   //
   // The two values below differ in BOTH the high 32 bits and the low
   // 32 bits, so a torn read would produce a mask that equals neither
   // -- and the test would fail.

   const auto track = gTrackMaker.Track(WaveClipHolders {});

   constexpr uint64_t kValueA = 0x1111'1111'AAAA'AAAAull;
   constexpr uint64_t kValueB = 0xFFFF'FFFF'5555'5555ull;

   track->SetPlaybackOutputMask(kValueA);

   std::atomic<bool> stop { false };
   std::atomic<uint64_t> tornReadCount { 0 };
   std::atomic<uint64_t> readCount { 0 };

   std::thread reader([&] {
      while (!stop.load(std::memory_order_relaxed)) {
         const auto value = track->GetPlaybackOutputMask();
         ++readCount;
         if (value != kValueA && value != kValueB)
            ++tornReadCount;
      }
   });

   std::thread writer([&] {
      for (int i = 0; i < 100000; ++i) {
         track->SetPlaybackOutputMask((i & 1) ? kValueB : kValueA);
      }
   });

   writer.join();
   stop.store(true, std::memory_order_relaxed);
   reader.join();

   // The reader must have observed only the two intended values --
   // never a torn mix of the high half of one and the low half of the
   // other.  We also require the reader ran enough iterations to make
   // this a meaningful assertion on a typical dev box; if this ever
   // flakes due to scheduler contention, bump the writer's loop count.
   REQUIRE(tornReadCount.load() == 0);
   REQUIRE(readCount.load() > 0);
}
