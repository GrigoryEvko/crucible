// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004y (#1287): Senses::load_all() takes a mandatory `effects::Init`
// capability tag.  No default — even a typo that drops the argument
// fails compilation rather than silently constructing a Senses without
// the cap.  A future maintainer who adds `= effects::Init{}` as a
// default would silently weaken the gate, and this fixture catches
// that regression at the earliest CI point.
//
// Violation: calls load_all() with no argument.
// Expected diagnostic regex: "too few arguments|no matching function|
// candidate expects 1 argument".

#include <crucible/perf/Senses.h>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    auto s = crucible::perf::Senses::load_all();

    (void)s;
    return 0;
}
