/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrackOutputMaskTest.cpp

  Tests for WaveTrack::Get/SetPlaybackOutputMask and the XML
  serialization of the outputmasklo/outputmaskhi attributes (and the
  legacy outputmask attribute for backward compatibility).

  Placed in lib-stretching-sequence/tests because that test target
  already links lib-wave-track and has the TestWaveTrackMaker helper.

**********************************************************************/
#include "MockSampleBlockFactory.h"
#include "TestWaveTrackMaker.h"

#include "WaveTrack.h"
#include "XMLFileReader.h"
#include "XMLWriter.h"

#include <catch2/catch.hpp>

#include <cstdint>
#include <memory>

namespace
{
constexpr int kSampleRate = 44100;

const auto gSampleBlockFactory = std::make_shared<MockSampleBlockFactory>();
TestWaveTrackMaker gTrackMaker { kSampleRate, gSampleBlockFactory };

wxString WriteWaveTrackToString(const WaveTrack& track)
{
   XMLStringWriter writer;
   track.WriteXML(writer);
   return writer;
}
} // namespace

TEST_CASE(
   "WaveTrack: default playback output mask is empty",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask().empty());
}

TEST_CASE(
   "WaveTrack: set then get round-trips a low-word mask",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   PlaybackOutputMask m;
   m.lo = 0x12345678ull;
   track->SetPlaybackOutputMask(m);
   REQUIRE(track->GetPlaybackOutputMask() == m);
}

TEST_CASE(
   "WaveTrack: set then get round-trips a high-word mask",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   PlaybackOutputMask m;
   m.hi = 0xDEADBEEFull;
   track->SetPlaybackOutputMask(m);
   REQUIRE(track->GetPlaybackOutputMask() == m);
}

TEST_CASE(
   "WaveTrack: clearing the mask (empty) round-trips",
   "[WaveTrack][OutputMask]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask({ 0xCAFEull, 0 });
   REQUIRE_FALSE(track->GetPlaybackOutputMask().empty());
   track->SetPlaybackOutputMask({});
   REQUIRE(track->GetPlaybackOutputMask().empty());
}

TEST_CASE(
   "WaveTrack: mask survives Clone",
   "[WaveTrack][OutputMask]")
{
   const auto original = gTrackMaker.Track(WaveClipHolders {});
   PlaybackOutputMask m{ 0xABCDEFull, 0x100ull };
   original->SetPlaybackOutputMask(m);

   const auto copy = std::static_pointer_cast<WaveTrack>(
      original->Duplicate());
   REQUIRE(copy != nullptr);
   REQUIRE(copy->GetPlaybackOutputMask() == m);
}

TEST_CASE(
   "WaveTrack XML: empty mask writes outputmasklo=0 outputmaskhi=0",
   "[WaveTrack][OutputMask][XML]")
{
   // The new format always writes both words, even for empty masks,
   // so load-time migration can tell "user chose silent" (attrs
   // present, mask empty) from "legacy project without routing
   // metadata" (no attrs at all).
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask().empty());

   const auto xml = WriteWaveTrackToString(*track);
   REQUIRE(xml.Find("outputmasklo=\"0\"") != wxNOT_FOUND);
   REQUIRE(xml.Find("outputmaskhi=\"0\"") != wxNOT_FOUND);
}

TEST_CASE(
   "WaveTrack XML: non-empty mask writes both words",
   "[WaveTrack][OutputMask][XML]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   track->SetPlaybackOutputMask({ 0xCAFEull, 0x42ull });

   const auto xml = WriteWaveTrackToString(*track);
   // 0xCAFE = 51966, 0x42 = 66
   REQUIRE(xml.Find("outputmasklo=\"51966\"") != wxNOT_FOUND);
   REQUIRE(xml.Find("outputmaskhi=\"66\"") != wxNOT_FOUND);
}

TEST_CASE(
   "WaveTrack XML: reading outputmasklo / outputmaskhi sets the mask",
   "[WaveTrack][OutputMask][XML]")
{
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetPlaybackOutputMask().empty());
   REQUIRE(track->GetOutputMaskAttrSeen());

   const unsigned long long kLo = 0xC0FFEEull;
   const unsigned long long kHi = 0x1234ull;
   AttributesList attrs {
      { std::string_view { "outputmasklo" },
        XMLAttributeValueView { kLo } },
      { std::string_view { "outputmaskhi" },
        XMLAttributeValueView { kHi } }
   };
   REQUIRE(track->HandleXMLTag(WaveTrack::WaveTrack_tag, attrs));
   REQUIRE(track->GetPlaybackOutputMask().lo ==
      static_cast<uint64_t>(kLo));
   REQUIRE(track->GetPlaybackOutputMask().hi ==
      static_cast<uint64_t>(kHi));
   REQUIRE(track->GetOutputMaskAttrSeen());
}

TEST_CASE(
   "WaveTrack XML: tag WITHOUT any outputmask attrs clears the seen flag",
   "[WaveTrack][OutputMask][XML]")
{
   // Legacy/upstream project path: no attrs -> seen=false so the
   // post-load migration will synthesize identity routing.
   const auto track = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(track->GetOutputMaskAttrSeen()); // in-app created: default true

   AttributesList attrs {}; // no attributes at all
   REQUIRE(track->HandleXMLTag(WaveTrack::WaveTrack_tag, attrs));
   REQUIRE(track->GetPlaybackOutputMask().empty());
   REQUIRE_FALSE(track->GetOutputMaskAttrSeen());
}

TEST_CASE(
   "WaveTrack XML: full round-trip via XMLFileReader preserves the mask",
   "[WaveTrack][OutputMask][XML]")
{
   const auto source = gTrackMaker.Track(WaveClipHolders {});
   source->SetPlaybackOutputMask({ 0xFEEDFACEull, 0xCAFEF00Dull });

   const auto xml = WriteWaveTrackToString(*source);
   REQUIRE(xml.Find("outputmasklo") != wxNOT_FOUND);
   REQUIRE(xml.Find("outputmaskhi") != wxNOT_FOUND);

   const auto target = gTrackMaker.Track(WaveClipHolders {});
   REQUIRE(target->GetPlaybackOutputMask().empty());

   XMLFileReader reader;
   const auto ok = reader.ParseString(target.get(), xml);
   (void)ok;
   REQUIRE(target->GetPlaybackOutputMask().lo == 0xFEEDFACEull);
   REQUIRE(target->GetPlaybackOutputMask().hi == 0xCAFEF00Dull);
}
