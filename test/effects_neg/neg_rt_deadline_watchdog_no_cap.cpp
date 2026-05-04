// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004g (#1283): rt::DeadlineWatchdog construction takes a
// mandatory `effects::Init` capability tag.  No default — a typo
// dropping the third argument fails compilation rather than silently
// constructing without the capability gate.  A future maintainer who
// adds `= effects::Init{}` as a default would weaken the gate, and
// this fixture catches that regression at the earliest CI point.
//
// Same Init-by-value discipline as every Senses-touching surface in
// the GAPS-004 series.
//
// Violation: constructs DeadlineWatchdog with no capability argument.
// Expected diagnostic regex: "too few arguments|no matching function|
// candidate expects 3 arguments".

#include <crucible/rt/DeadlineWatchdog.h>
#include <crucible/rt/Policy.h>

int main() {
    crucible::rt::Policy policy = crucible::rt::Policy::production();

    // <-- this line must NOT compile (missing Init argument)
    crucible::rt::DeadlineWatchdog wd{nullptr, policy};

    (void)wd;
    return 0;
}
