// Negative-compile test. This file must fail to compile.
//
// load_pmu_sample takes the startup load context, and the parameter has
// no default.  A call with no argument has too few arguments, so no
// caller loads the program without the evidence of process startup.

#include <crucible/perf/PmuSample.h>

int main() {
    ::fixy::Option<crucible::perf::PmuSample> hub = crucible::perf::load_pmu_sample();
    (void)hub;
    return 0;
}
