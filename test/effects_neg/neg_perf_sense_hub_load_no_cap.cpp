// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004a-AUDIT (#1288): SenseHub::load() takes a mandatory
// `effects::Init` capability tag.  The parameter has NO default, so
// even a typo that drops the argument at the call site fails to
// compile rather than silently calling a stub.  This is load-bearing
// for the "Init context is required" claim — a future maintainer who
// adds `= effects::Init{}` as a default would silently weaken the
// gate (any caller could now call without holding the cap), and
// this fixture catches that regression at the earliest possible
// point in CI.
//
// Violation: calls load() with no argument.  The compiler should
// fail with "too few arguments" / "no matching function".
//
// Expected diagnostic: "too few arguments|no matching function|
// candidate expects 1 argument" — toolchain-portable witnesses
// of "the parameter is mandatory".

#include <crucible/perf/SenseHub.h>

#include <optional>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    std::optional<crucible::perf::SenseHub> hub =
        crucible::perf::SenseHub::load();

    (void)hub;
    return 0;
}
