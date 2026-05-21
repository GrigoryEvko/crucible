// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type that stores a CKernelTable::MutableView as a
// non-static data member.  Fires the Tier-2 reflection audit
// `no_scoped_view_field_check`, which recursively walks the member's
// type tree and rejects any ScopedView found.
//
// CKernelTable::MutableView is the carrier-locked type alias for
// `crucible::fixy::wrap::ScopedView<CKernelTable, ckernel_state::Mutable>`.
// Reflection sees through the alias and triggers the same audit that
// fires for the explicit ScopedView spelling — proof that the
// discipline is alias-stable.
//
// CKernelTable itself opts in to this audit at CKernel.h:595 with
//     static_assert(crucible::fixy::wrap::no_scoped_view_field_check<CKernelTable>());
// — that asserts CKernelTable itself does NOT store a view.  This
// fixture asserts the AUDIT is also live for INDEPENDENT carrier
// types that try to embed a CKernelTable::MutableView.
//
// HS14 — distinct mismatch class from the companion
// neg_ckernel_mint_mutable_view_to_sealed_overload.cpp:
//   * That one catches typed-overload resolution at call time (wrong
//     Tag in argument position).
//   * THIS one catches structural lifetime escape at field-storage
//     time (view stored as struct field — would outlive the carrier
//     that minted it).
//
// Together they pin BOTH soundness gates of CKernelTable::mint_mutable_view.

#include <crucible/CKernel.h>

// Anchor `mint_mutable_view` in the source so the HS14 fixture scanner
// (rg "\bmint_mutable_view\b" across test/*_neg/) counts this file.
// The anchor itself does not change the discipline being tested — the
// static_assert below is the actual neg-compile gate.
[[maybe_unused]] static auto anchor_mutable_view_mint() {
    crucible::CKernelTable t;
    return t.mint_mutable_view();
}

// A class with a CKernelTable::MutableView field.  The audit walks
// `view_`'s declared type, sees a ScopedView<CKernelTable, Mutable>
// hiding behind the typedef, and rejects the audit.  No constructor
// is declared — the audit reads layout from the declaration; the
// class need never be instantiated.
struct OffendingContainer {
    crucible::CKernelTable::MutableView view_;
};

// Trigger the audit at compile time.  GCC's diagnostic names
// OffendingContainer as the violator and ScopedView as the offending
// wrapper.
static_assert(::crucible::fixy::wrap::no_scoped_view_field_check<OffendingContainer>(),
    "the audit must reject containers that store a CKernelTable::"
    "MutableView as a field; this fixture exists so a future "
    "regression in contains_scoped_view's recursive walk is caught "
    "at compile time.");

int main() { return 0; }
