// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A raw `const TraceEntry*` does not convert to the
// `Tagged<const TraceEntry*, source::RegionOps>` slot type of RegionCache.
// The constructor of Tagged from its value is private, so the one door is
// mint_tagged<source::RegionOps>(ptr), which names the provenance at the
// call site.  Without this gate a pointer from another source (a test
// buffer, an arena throwaway, a freed buffer) could take a slot of ops_
// with no call that states where it came from.
//
// Companion: neg_region_ops_ptr_cross_source_assignment.cpp refuses a
// pointer under a different source tag.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Quarantine.h>

namespace crucible {
struct FakeTraceEntry {
    int dummy;
};
}  // namespace crucible

int main() {
    using OpsPtr = ::fixy::Tagged<const crucible::FakeTraceEntry*, ::fixy::tags::source::RegionOps>;

    crucible::FakeTraceEntry entry{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    const crucible::FakeTraceEntry* raw_ops = &entry;

    OpsPtr field = raw_ops;
    (void)field;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
