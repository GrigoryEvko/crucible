// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004f (#1282): SchedTpBtf::load() takes `effects::Init` by
// value as its sole parameter.  Bg / Init / Test are distinct
// 1-byte capability structs with no implicit conversion between them
// — a hot-path frame that holds only `Bg` cannot accidentally reach
// the startup-only loader.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// Expected diagnostic: "could not convert|no matching function|
// cannot convert|expected.*Init".

#include <crucible/perf/SchedTpBtf.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile (Bg cap, Init required)
    std::optional<crucible::perf::SchedTpBtf> hub =
        crucible::perf::SchedTpBtf::load(bg_cap);

    (void)hub;
    return 0;
}
