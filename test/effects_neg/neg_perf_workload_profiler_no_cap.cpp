// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004h (#1284): WorkloadProfiler ctor takes a mandatory
// `effects::Init` capability tag as its second positional parameter.
// No default — even a typo that drops the argument fails compilation
// rather than silently calling a stub.  A future maintainer who adds
// `= effects::Init{}` as a default would silently weaken the gate
// (any caller could construct without holding the cap), and this
// fixture catches that regression at the earliest CI point.
//
// Violation: constructs WorkloadProfiler with only the senses
// pointer, no Init.
// Expected diagnostic: "too few arguments|no matching function|
// candidate expects 2-3 arguments".

#include <crucible/perf/WorkloadProfiler.h>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    crucible::perf::WorkloadProfiler profiler{nullptr};

    (void)profiler;
    return 0;
}
