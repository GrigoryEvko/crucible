// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004b-AUDIT (#1289): SchedSwitch::load() takes a `effects::Init`
// capability tag by value as its sole parameter.  Same gate as
// SenseHub::load — Bg / Init / Test are distinct 1-byte structs with
// no implicit conversion between them, so a hot-path or background
// frame that holds only `Bg` cannot accidentally reach the
// startup-only loader.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// The compiler should fail with "no matching function" / "could not
// convert" / "expected" pointing at the parameter type mismatch.
//
// Expected diagnostic: "could not convert|no matching function|cannot
// convert|expected.*Init" — toolchain-portable witness of the gate.

#include <crucible/perf/SchedSwitch.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile
    std::optional<crucible::perf::SchedSwitch> hub =
        crucible::perf::SchedSwitch::load(bg_cap);

    (void)hub;
    return 0;
}
