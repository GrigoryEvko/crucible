// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RegionCache-2 #987, mismatch class #1 of 2:
// RAW `const TraceEntry*` CANNOT BE ASSIGNED DIRECTLY TO A
// `Tagged<const TraceEntry*, source::RegionOps>` SLOT.
//
// `Tagged<T, S>` requires explicit construction via `Tagged<T, S>{value}`
// — the `explicit` ctor refuses implicit conversion from the raw `T`
// pointer.  This catches the production-side defect mode where a
// caller does `ops_[slot] = region->ops;` (raw assignment) when the
// migration intent was `ops_[slot] = OpsPtr{region->ops};` (typed
// construction).  Without this gate, a `const TraceEntry*` from a
// non-RegionNode source (heap-allocated test fixture, arena throwaway,
// freed buffer) could silently leak into RegionCache's ops_[] array
// and the source::RegionOps invariant ("extracted from RegionNode.ops
// at cache-insertion time, paired with a live WeakRef slot") would be
// subverted.
//
// Companion fixture: neg_region_ops_ptr_cross_source_assignment.cpp
// catches provenance LAUNDERING.  This one catches provenance BYPASS.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate.  Mirror
// of the WRAP-CCtx-2 #904 active_region_ raw-assignment fixture (same
// Tagged shape, different source tag / pointee type).

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeTraceEntry { int dummy; };
}

int main() {
    using OpsPtr = ::crucible::safety::Tagged<
        const crucible::FakeTraceEntry*, ::crucible::safety::source::RegionOps>;

    crucible::FakeTraceEntry entry{};
    const crucible::FakeTraceEntry* raw_ops = &entry;

    // Should FAIL: implicit conversion from raw `const TraceEntry*` to
    // Tagged<const TraceEntry*, source::RegionOps> is rejected by the
    // explicit ctor.  Migration intent is `OpsPtr{raw_ops}` — never
    // `= raw_ops`.
    OpsPtr field = raw_ops;
    (void)field;
    return 0;
}
