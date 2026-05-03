// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004b-AUDIT (#1289): SchedSwitch::load() takes a mandatory
// `effects::Init` capability tag.  No default — even a typo that
// drops the argument fails compilation rather than silently calling
// a stub.  A future maintainer who adds `= effects::Init{}` as a
// default would silently weaken the gate (any caller could call
// without holding the cap), and this fixture catches that
// regression at the earliest CI point.
//
// Violation: calls load() with no argument.
// Expected diagnostic: "too few arguments|no matching function|
// candidate expects 1 argument".

#include <crucible/perf/SchedSwitch.h>

#include <optional>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    std::optional<crucible::perf::SchedSwitch> hub =
        crucible::perf::SchedSwitch::load();

    (void)hub;
    return 0;
}
