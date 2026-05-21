// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing the result of SchemaTable::mint_mutable_view() to
// a helper that expects a SchemaTable::SealedView.  The two views share
// the same Carrier (SchemaTable) but differ in their Tag
// (schema_state::Mutable vs schema_state::Sealed) — and ScopedView<C,
// A> is NOT implicitly convertible to ScopedView<C, B> when A != B.
//
// Sibling fixture to neg_ckernel_mint_mutable_view_to_sealed_overload
// (shipped by FIXY-U-135): same shape, different carrier.  Even though
// the HS14 scanner counts `mint_mutable_view` as a shared name across
// both carriers, the underlying ScopedView types are distinct, the
// view_ok ADL overloads are distinct, and a regression in EITHER
// carrier's per-state discipline needs an independent witness.  FIXY-
// U-136 closes the "spirit gap" surfaced by U-135's audit: a future
// edit that breaks SchemaTable's mint_mutable_view discipline but not
// CKernelTable's would not be caught by U-135's fixtures alone.
//
// HS14 — paired with neg_schema_mint_mutable_view_in_field for distinct
// mismatch classes (value-level call-time vs structural field-storage).

#include <crucible/SchemaTable.h>

// A fixture-local helper that consumes a SealedView.  No production
// API has this exact shape; the helper exists so the type system can
// be asked to convert MutableView → SealedView, which it must refuse.
static void requires_sealed_view(
    crucible::SchemaTable::SealedView const&) noexcept {}

int main() {
    crucible::SchemaTable t;
    // t is in Mutable state (default-constructed, sealed_ = false).
    // mint_mutable_view's pre `!is_sealed()` is satisfied, so the view
    // is produced legitimately.
    auto mv = t.mint_mutable_view();

    // requires_sealed_view takes ScopedView<SchemaTable, Sealed>.  mv
    // is ScopedView<SchemaTable, Mutable>.  Distinct template
    // instantiations: GCC rejects the call at overload resolution.
    requires_sealed_view(mv);
    return 0;
}
