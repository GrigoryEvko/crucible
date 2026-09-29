// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SchedSwitch::load takes the startup load context, and the parameter
// has no default.  A call with no argument finds no matching function,
// so no caller loads the program without the evidence of process
// startup.

#include <crucible/perf/SchedSwitch.h>

#include <optional>

int main() {
    std::optional<crucible::perf::SchedSwitch> hub = crucible::perf::SchedSwitch::load();
    (void)hub;
    return 0;
}
