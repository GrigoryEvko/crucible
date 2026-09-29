// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A `Tagged<const TraceEntry*, source::Arena>` does not convert to the
// `Tagged<const TraceEntry*, source::RegionOps>` slot type of RegionCache.
// The two wrap the same pointer type, but the tag makes them distinct
// types, and no retag edge joins Arena to RegionOps.  An arena pointer is
// freed at arena reset, and a slot of ops_ must hold a pointer that pairs
// with the live region in the same slot of regions_.
//
// Companion: neg_region_ops_ptr_raw_assignment.cpp refuses an untagged
// pointer.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeTraceEntry {
    int dummy;
};
}  // namespace crucible

int main() {
    using RegionOpsPtr = ::fixy::Tagged<const crucible::FakeTraceEntry*, ::fixy::tags::source::RegionOps>;

    const crucible::FakeTraceEntry entry{};
    auto arena_tagged = ::fixy::mint_tagged<::fixy::tags::source::Arena>(&entry);

    RegionOpsPtr field = arena_tagged;
    (void)field;
    return 0;
}
