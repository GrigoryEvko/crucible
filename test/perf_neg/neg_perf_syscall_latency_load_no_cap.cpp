// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// load_syscall_latency takes the startup load context, and the parameter
// has no default.  A call with no argument has too few arguments, so no
// caller loads the program without the evidence of process startup.

#include <crucible/perf/SyscallLatency.h>

int main() {
    ::fixy::Option<crucible::perf::SyscallLatency> hub = crucible::perf::load_syscall_latency();
    (void)hub;
    return 0;
}
