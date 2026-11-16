// The kernel bumps the counters of the hub at times that the schedule
// decides, so two runs with the same inputs read two snapshots.  The read
// takes the bare tag of Nondet, and a read with no tag does not compile.
//
// Expected diagnostic: read has no candidate with no argument.

#include <crucible/perf/SenseHub.h>

namespace {

[[maybe_unused]] crucible::perf::Snapshot attempt(crucible::perf::SenseHub const& hub) noexcept { return hub.read(); }

}  // namespace

int main() { return 0; }
