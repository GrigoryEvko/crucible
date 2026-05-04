// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004e (#1281): SyscallLatency::load() takes a `effects::Init`
// capability tag by value as its sole parameter.  Same gate as
// SenseHub/SchedSwitch/PmuSample/LockContention::load — Bg / Init /
// Test are distinct 1-byte structs with no implicit conversion
// between them, so a hot-path or background frame that holds only
// `Bg` cannot accidentally reach the startup-only loader.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// Expected diagnostic regex: "could not convert|no matching function|
// cannot convert|expected.*Init".

#include <crucible/perf/SyscallLatency.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile
    std::optional<crucible::perf::SyscallLatency> hub =
        crucible::perf::SyscallLatency::load(bg_cap);

    (void)hub;
    return 0;
}
