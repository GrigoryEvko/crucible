// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CCtx-2 #904 (Tagged half), mismatch class #2 of 2:
// `Tagged<T, source::Arena>` CANNOT BE ASSIGNED TO A
// `Tagged<T, source::Vigil>` FIELD WITHOUT EXPLICIT RETAG.
//
// Companion to neg_active_region_ptr_raw_assignment.cpp.  The raw-
// pointer fixture catches a caller bypassing the provenance gate
// entirely.  THIS fixture catches the SUBTLER defect mode:
// provenance LAUNDERING via cross-source mixing.  A caller has a
// `Tagged<const RegionNode*, source::Arena>` (e.g. an arena-allocated
// throwaway RegionNode built for a test fixture or speculative
// branch), and tries to assign it to the `Tagged<const RegionNode*,
// source::Vigil>` field.  Both wrap `const RegionNode*`, but the Tag
// distinguishes them as nominally distinct types and the type system
// refuses the swap.
//
// Without this gate, a RegionNode* from a different lifetime regime
// (arena: freed at arena reset / heap: caller-managed) would silently
// take residence in active_region_ and the source::Vigil invariant
// ("valid for the Vigil/BackgroundThread lifetime; observers see the
// bg worker's active_region store(release) happens-before fence")
// would be subverted — the active_region_ pointer would dangle as
// soon as the arena resets or the caller frees.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Mirror of the
// WRAP-Transaction-6 #1065 cross-source fixture (same Tagged shape,
// different source-tag axis: source::Arena vs source::Vigil).

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeRegionNode { int dummy; };
}

int main() {
    using VigilRegion = ::crucible::safety::Tagged<
        const crucible::FakeRegionNode*, ::crucible::safety::source::Vigil>;
    using ArenaRegion = ::crucible::safety::Tagged<
        const crucible::FakeRegionNode*, ::crucible::safety::source::Arena>;

    crucible::FakeRegionNode region{};
    ArenaRegion arena_tagged{&region};

    // Should FAIL: Tagged<T, source::Arena> and Tagged<T, source::Vigil>
    // are DISTINCT nominal types despite identical value_type T.  The
    // Tag is the type-level provenance witness; cross-source assignment
    // requires explicit `Tagged<T, NewTag>{old.value()}` re-wrapping
    // (provenance is re-asserted at the call site, not auto-laundered).
    VigilRegion field = arena_tagged;
    (void)field;
    return 0;
}
