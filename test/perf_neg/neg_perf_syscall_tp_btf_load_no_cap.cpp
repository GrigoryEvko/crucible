// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// load_syscall_tp_btf takes the startup load context, and the parameter
// has no default.  A call with no argument has too few arguments, so no
// caller loads the program without the evidence of process startup.

#include <crucible/perf/SyscallTpBtf.h>

int main() {
    ::fixy::Option<crucible::perf::SyscallTpBtf> hub = crucible::perf::load_syscall_tp_btf();
    (void)hub;
    return 0;
}
