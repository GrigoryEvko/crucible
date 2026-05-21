// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type that stores a PoolAllocator::InitializedView
// as a non-static data member.  Fires the Tier-2 reflection audit
// `no_scoped_view_field_check`, which recursively walks the member's
// type tree and rejects any ScopedView found.
//
// PoolAllocator::InitializedView is the carrier-locked type alias for
// `crucible::fixy::wrap::ScopedView<PoolAllocator, pool_state::Initialized>`.
// Reflection sees through the alias and triggers the same audit that
// fires for the explicit ScopedView spelling — proof that the
// discipline is alias-stable across the PoolAllocator carrier.
//
// HS14 — paired with neg_pool_allocator_mint_initialized_view_cross_carrier:
//   * That one catches typed-overload resolution at call time
//     (cross-carrier ScopedView mismatch).
//   * THIS one catches structural lifetime escape at field-storage time.
// Together they pin BOTH soundness gates of PoolAllocator::mint_initialized_view.

#include <crucible/PoolAllocator.h>

// Anchor `mint_initialized_view` in the source so the HS14 fixture
// scanner (rg "\bmint_initialized_view\b" across test/*_neg/) counts
// this file.  The anchor itself does not change the discipline being
// tested — the static_assert below is the actual neg-compile gate.
[[maybe_unused]] static auto anchor_initialized_view_mint() {
    crucible::PoolAllocator pool;
    // mint_initialized_view's pre `is_initialized()` would fire on the
    // default-constructed (ptr_table_ == nullptr) pool at runtime —
    // but this fixture is a compile-time test and runtime is never
    // reached.  The function-body-execution would also be suppressed
    // by the file's static_assert failure below.  The anchor exists
    // solely for grep-discoverability of the bare-name token.
    return pool.mint_initialized_view();
}

// A class with a PoolAllocator::InitializedView field.  The audit walks
// `view_`'s declared type, sees a ScopedView<PoolAllocator, Initialized>
// hiding behind the typedef, and rejects the audit.  No constructor
// is declared — the audit reads layout from the declaration; the
// class need never be instantiated.
struct OffendingPoolContainer {
    crucible::PoolAllocator::InitializedView view_;
};

// Trigger the audit at compile time.  GCC's diagnostic names
// OffendingPoolContainer as the violator and ScopedView as the
// offending wrapper.
static_assert(::crucible::fixy::wrap::no_scoped_view_field_check<OffendingPoolContainer>(),
    "the audit must reject containers that store a PoolAllocator::"
    "InitializedView as a field; this fixture exists so a future "
    "regression in contains_scoped_view's recursive walk for the "
    "PoolAllocator carrier is caught at compile time.");

int main() { return 0; }
