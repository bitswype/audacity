/*
* Audacity: A Digital Audio Editor
*
* Minimal test environment for au3-mixer tests.
* No Muse IoC or module dependencies needed -- these are pure unit tests.
*/

#include "testing/environment.h"

static muse::testing::SuiteEnvironment mixer_se
   = muse::testing::SuiteEnvironment();
