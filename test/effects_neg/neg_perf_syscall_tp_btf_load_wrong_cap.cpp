// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004f (#1282): SyscallTpBtf::load() takes `effects::Init` by
// value.  Bg / Init / Test are distinct 1-byte capability structs
// with no implicit conversion.
//
// Violation: passes `effects::testing::bg()` where `effects::testing::init()` is required.
// Expected diagnostic: "could not convert|no matching function|
// cannot convert|expected.*Init".

#include <crucible/perf/SyscallTpBtf.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    auto bg_cap = crucible::effects::testing::bg();

    // <-- this line must NOT compile (Bg cap, Init required)
    std::optional<crucible::perf::SyscallTpBtf> hub =
        crucible::perf::SyscallTpBtf::load(bg_cap);

    (void)hub;
    return 0;
}
