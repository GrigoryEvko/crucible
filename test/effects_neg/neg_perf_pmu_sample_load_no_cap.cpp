// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004c (#1279): PmuSample::load() takes a mandatory Init cap.

#include <crucible/perf/PmuSample.h>

#include <optional>

int main() {
    std::optional<crucible::perf::PmuSample> hub =
        crucible::perf::PmuSample::load();  // <-- must NOT compile
    (void)hub;
    return 0;
}
