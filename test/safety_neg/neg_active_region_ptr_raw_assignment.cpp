// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CCtx-2 #904 (Tagged half), mismatch class #1 of 2:
// RAW `const RegionNode*` CANNOT BE ASSIGNED DIRECTLY TO A
// `Tagged<const RegionNode*, source::Vigil>` FIELD.
//
// `Tagged<T, S>` requires explicit construction via `Tagged<T, S>{value}`
// — the `explicit` ctor refuses implicit conversion from the raw `T`
// pointer.  This catches the production-side defect mode where a
// caller does `active_region_ = some_region` (raw assignment) when
// the migration intent was `active_region_ = ActiveRegionPtr{some_region}`
// (typed construction).  Without this gate, a fresh-heap-allocated or
// arena-allocated RegionNode* (NOT published by Vigil's bg worker via
// active_region store(release)) could leak into CrucibleContext's
// active_region_ field, breaking the Vigil-published-ordering invariant
// (active_region store(release) happens-before dispatch_op data reads).
//
// Companion fixture: neg_active_region_ptr_cross_source_assignment.cpp
//   * That one catches cross-source mixing (Tagged<...,source::Arena>
//     → Tagged<...,source::Vigil>) — provenance LAUNDERING.
//   * This one catches raw-pointer admission — provenance BYPASS.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Mirror of the
// WRAP-Transaction-6 #1065 raw-assignment fixture (same Tagged shape,
// different source tag / pointee type).

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeRegionNode { int dummy; };
}

int main() {
    using ActiveRegionPtr = ::crucible::safety::Tagged<
        const crucible::FakeRegionNode*, ::crucible::safety::source::Vigil>;

    crucible::FakeRegionNode region{};
    const crucible::FakeRegionNode* raw_ptr = &region;

    // Should FAIL: implicit conversion from raw const RegionNode* to
    // Tagged<const RegionNode*, source::Vigil> is rejected by the
    // explicit ctor.  Migration intent is `ActiveRegionPtr{raw_ptr}`
    // — never `= raw_ptr`.
    ActiveRegionPtr field = raw_ptr;
    (void)field;
    return 0;
}
