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

namespace crucible {
struct FakeTraceEntry {
    int dummy;
};
}  // namespace crucible

int main() {
    using OpsPtr = ::fixy::Tagged<const crucible::FakeTraceEntry*, ::fixy::tags::source::RegionOps>;

    crucible::FakeTraceEntry entry{};
    const crucible::FakeTraceEntry* raw_ops = &entry;

    OpsPtr field = raw_ops;
    (void)field;
    return 0;
}
