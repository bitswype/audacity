#  SPDX-License-Identifier: GPL-2.0-or-later
#[[
Unit tests for lib-mixer
]]

add_unit_test(
   NAME
      lib-mixer
   SOURCES
      AtomicPlaybackOutputMaskTest.cpp
      ChannelRoutingTest.cpp
      PlaybackOutputMaskTest.cpp
      RouteTrackSamplesTest.cpp
   LIBRARIES
      lib-mixer
)
