set(MODULE_TEST au3_mixer_tests)

set(MODULE_TEST_SRC
   ${CMAKE_CURRENT_LIST_DIR}/DownmixSourceTests.cpp
   ${CMAKE_CURRENT_LIST_DIR}/DownmixStageTests.cpp
)

set(MODULE_TEST_LINK
   au3-mixer
   au3-audio-graph
   au3-math
   au3-utility
)

include(SetupGTest)
