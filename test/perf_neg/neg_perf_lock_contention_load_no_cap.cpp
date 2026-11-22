// Negative-compile test. This file must fail to compile.
//
// load_lock_contention takes the startup load context, and the parameter
// has no default.  A call with no argument has too few arguments, so no
// caller loads the program without the evidence of process startup.

#include <crucible/perf/LockContention.h>

int main() {
    ::fixy::Option<crucible::perf::LockContention> hub = crucible::perf::load_lock_contention();
    (void)hub;
    return 0;
}
