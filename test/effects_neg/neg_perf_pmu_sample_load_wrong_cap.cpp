// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004c (#1279): PmuSample::load() takes effects::Init by value.
// Same cap-typing gate as SenseHub/SchedSwitch — Bg/Init/Test are
// distinct 1-byte structs with no implicit conversion.

#include <crucible/perf/PmuSample.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    crucible::effects::Bg bg_cap{};
    std::optional<crucible::perf::PmuSample> hub =
        crucible::perf::PmuSample::load(bg_cap);  // <-- must NOT compile
    (void)hub;
    return 0;
}
