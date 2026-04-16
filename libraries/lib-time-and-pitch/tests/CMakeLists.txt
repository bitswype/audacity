#[[
Unit tests for lib-time-and-pitch
]]

add_compile_definitions(CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_unit_test(
   NAME
      lib-time-and-pitch
   WAV_FILE_IO
   MOCK_PREFS
   SOURCES
      StaffPadTimeAndPitchTest.cpp
      TimeAndPitchFakeSource.h
      TimeAndPitchRealSource.h
   LIBRARIES
      lib-utility
      lib-time-and-pitch-interface
)

# The "Extreme stretch ratios" test compresses 60s of audio into 1
# sample through the FFT vocoder.  In Debug builds this takes 10+
# minutes (upstream CI uses RelWithDebInfo where it completes quickly).
# Skip in Debug to keep the test suite fast.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set_tests_properties(lib-time-and-pitch PROPERTIES DISABLED TRUE)
endif()
