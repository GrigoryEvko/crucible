// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RegionCache-2 #987, mismatch class #2 of 2:
// `Tagged<const TraceEntry*, source::Arena>` CANNOT BE ASSIGNED TO A
// `Tagged<const TraceEntry*, source::RegionOps>` SLOT WITHOUT EXPLICIT RETAG.
//
// Companion to neg_region_ops_ptr_raw_assignment.cpp.  The raw-pointer
// fixture catches provenance BYPASS.  THIS fixture catches the SUBTLER
// defect mode: provenance LAUNDERING via cross-source mixing.  A caller
// has a `Tagged<const TraceEntry*, source::Arena>` (e.g. an arena-allocated
// throwaway TraceEntry built for a test fixture or speculative branch),
// and tries to assign it to the `Tagged<const TraceEntry*, source::RegionOps>`
// slot.  Both wrap `const TraceEntry*`, but the Tag distinguishes them as
// nominally distinct types and the type system refuses the swap.
//
// Without this gate, a TraceEntry* from a different lifetime regime
// (arena: freed at arena reset / heap: caller-managed) would silently
// take residence in ops_[] and the source::RegionOps invariant ("paired
// with a live WeakRef<RegionNode> at the same array index") would be
// subverted.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate.  Mirror
// of the WRAP-CCtx-2 #904 active_region_ cross-source fixture (same
// Tagged shape, different source-tag axis: source::Arena vs source::RegionOps).

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeTraceEntry { int dummy; };
}

int main() {
    using RegionOpsPtr = ::crucible::safety::Tagged<
        const crucible::FakeTraceEntry*, ::crucible::safety::source::RegionOps>;
    using ArenaOpsPtr  = ::crucible::safety::Tagged<
        const crucible::FakeTraceEntry*, ::crucible::safety::source::Arena>;

    crucible::FakeTraceEntry entry{};
    ArenaOpsPtr arena_tagged{&entry};

    // Should FAIL: Tagged<T, source::Arena> and Tagged<T, source::RegionOps>
    // are DISTINCT nominal types despite identical value_type T.  The
    // Tag is the type-level provenance witness; cross-source assignment
    // requires explicit `Tagged<T, NewTag>{old.value()}` re-wrapping
    // (provenance is re-asserted at the call site, not auto-laundered).
    RegionOpsPtr field = arena_tagged;
    (void)field;
    return 0;
}
