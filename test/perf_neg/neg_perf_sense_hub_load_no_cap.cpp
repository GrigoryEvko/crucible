// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// load_sense_hub takes the startup load context, and the parameter has no
// default.  A call with no argument has too few arguments, so no caller
// loads the program without the evidence of process startup.

#include <crucible/perf/SenseHub.h>

int main() {
    ::fixy::Option<crucible::perf::SenseHub> hub = crucible::perf::load_sense_hub();
    (void)hub;
    return 0;
}
