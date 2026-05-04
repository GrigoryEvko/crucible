// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004h (#1284): WorkloadProfiler ctor takes a `effects::Init`
// capability tag by value.  Same gate as every Senses / SenseHub /
// SchedSwitch / etc. construction in the perf tree — Bg / Init / Test
// are distinct 1-byte structs with no implicit conversion between
// them, so a hot-path or background frame that holds only `Bg` cannot
// accidentally construct a profiler.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// The compiler should fail with "no matching function" / "could not
// convert" / "expected" pointing at the parameter type mismatch.
//
// Expected diagnostic: "could not convert|no matching function|cannot
// convert|expected.*Init" — toolchain-portable witness of the gate.

#include <crucible/effects/Capabilities.h>
#include <crucible/perf/WorkloadProfiler.h>

int main() {
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile
    crucible::perf::WorkloadProfiler profiler{nullptr, bg_cap};

    (void)profiler;
    return 0;
}
