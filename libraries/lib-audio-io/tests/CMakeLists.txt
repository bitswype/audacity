#  SPDX-License-Identifier: GPL-2.0-or-later
#[[
Unit tests for lib-audio-io.
]]

add_unit_test(
   NAME
      lib-audio-io
   SOURCES
      TestToneGeneratorTest.cpp
   LIBRARIES
      lib-audio-io
)
