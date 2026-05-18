// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004y (#1287): Senses::load_all() takes a `effects::Init`
// capability tag by value as its sole parameter.  Same gate as the
// underlying SenseHub/SchedSwitch/PmuSample/LockContention/SyscallLatency
// facades — Bg / Init / Test are distinct 1-byte structs with no
// implicit conversion between them, so a hot-path or background frame
// that holds only `Bg` cannot accidentally reach the startup-only
// aggregator.
//
// Violation: passes `effects::testing::bg()` where `effects::testing::init()` is required.
// Expected diagnostic regex: "could not convert|no matching function|
// cannot convert|expected.*Init".

#include <crucible/perf/Senses.h>
#include <crucible/effects/Capabilities.h>

int main() {
    auto bg_cap = crucible::effects::testing::bg();

    // <-- this line must NOT compile
    auto s = crucible::perf::Senses::load_all(bg_cap);

    (void)s;
    return 0;
}
