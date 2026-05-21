// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing the result of CKernelTable::mint_mutable_view()
// to a helper that expects a CKernelTable::SealedView.  The two views
// share the same Carrier (CKernelTable) but differ in their Tag
// (ckernel_state::Mutable vs ckernel_state::Sealed) — and ScopedView<C,
// A> is NOT implicitly convertible to ScopedView<C, B> when A != B.
//
// This is the dual of neg_ckernel_register_with_sealed_view.cpp, which
// proves the type-state discipline rejects SealedView where MutableView
// is expected (the production register_op typed overload).  The pair
// together proves the per-state lock is symmetric: NEITHER MutableView
// nor SealedView can substitute for the other, even when both name the
// same Carrier.
//
// HS14 — distinct mismatch class from neg_ckernel_mint_mutable_view_in_field
// (which catches the structural Tier-2 field-storage audit).  This
// fixture catches the value-level typed-overload-resolution gate at
// CALL TIME — the type system rejects the wrong-state view at the
// argument boundary, before the helper's body is reachable.

#include <crucible/CKernel.h>

// A fixture-local helper that consumes a SealedView.  No production
// API currently has this exact shape; the helper exists so the type
// system can be asked to convert MutableView → SealedView, which it
// must refuse.
static void requires_sealed_view(
    crucible::CKernelTable::SealedView const&) noexcept {}

int main() {
    crucible::CKernelTable t;
    // t is in Mutable state (default-constructed, sealed_ = false).
    // mint_mutable_view's pre `!is_sealed()` is satisfied, so the view
    // is produced legitimately.
    auto mv = t.mint_mutable_view();

    // requires_sealed_view takes ScopedView<CKernelTable, Sealed>.  mv
    // is ScopedView<CKernelTable, Mutable>.  Distinct template
    // instantiations: GCC rejects the call at overload resolution.
    requires_sealed_view(mv);
    return 0;
}
