// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type that stores a SchemaTable::MutableView as a
// non-static data member.  Fires the Tier-2 reflection audit
// `no_scoped_view_field_check`, which recursively walks the member's
// type tree and rejects any ScopedView found.
//
// SchemaTable::MutableView is the carrier-locked type alias for
// `crucible::fixy::wrap::ScopedView<SchemaTable, schema_state::Mutable>`.
// Reflection sees through the alias and triggers the same audit that
// fires for the explicit ScopedView spelling — proof that the
// discipline is alias-stable across the SchemaTable carrier.
//
// SchemaTable itself opts in to this audit at SchemaTable.h:364 with
//     static_assert(crucible::fixy::wrap::no_scoped_view_field_check<SchemaTable>());
// — that asserts SchemaTable itself does NOT store a view.  This
// fixture asserts the AUDIT is also live for INDEPENDENT carrier
// types that try to embed a SchemaTable::MutableView.
//
// Sibling fixture to neg_ckernel_mint_mutable_view_in_field (shipped
// by FIXY-U-135): same shape, different carrier.  FIXY-U-136 ships
// genuine carrier-specific coverage; a future edit that breaks
// SchemaTable's no_scoped_view_field_check<SchemaTable::MutableView>
// instantiation (e.g. by deleting the schema_state::Mutable
// specialization of sv_unwrap_single, or by adding a SchemaTable-
// specific override that bypasses the recursion) would be caught
// here, not by CKernelTable-only fixtures.
//
// HS14 — paired with neg_schema_mint_mutable_view_to_sealed_overload:
//   * That one catches typed-overload resolution at call time.
//   * THIS one catches structural lifetime escape at field-storage time.
// Together they pin BOTH soundness gates of SchemaTable::mint_mutable_view.

#include <crucible/SchemaTable.h>

// Anchor `mint_mutable_view` in the source so the HS14 fixture scanner
// (rg "\bmint_mutable_view\b" across test/*_neg/) counts this file
// independently of the CKernelTable companion.  The anchor itself
// does not change the discipline being tested — the static_assert
// below is the actual neg-compile gate.
[[maybe_unused]] static auto anchor_schema_mutable_view_mint() {
    crucible::SchemaTable t;
    return t.mint_mutable_view();
}

// A class with a SchemaTable::MutableView field.  The audit walks
// `view_`'s declared type, sees a ScopedView<SchemaTable, Mutable>
// hiding behind the typedef, and rejects the audit.  No constructor
// is declared — the audit reads layout from the declaration; the
// class need never be instantiated.
struct OffendingSchemaContainer {
    crucible::SchemaTable::MutableView view_;
};

// Trigger the audit at compile time.  GCC's diagnostic names
// OffendingSchemaContainer as the violator and ScopedView as the
// offending wrapper.
static_assert(::crucible::fixy::wrap::no_scoped_view_field_check<OffendingSchemaContainer>(),
    "the audit must reject containers that store a SchemaTable::"
    "MutableView as a field; this fixture exists so a future "
    "regression in contains_scoped_view's recursive walk for the "
    "SchemaTable carrier is caught at compile time.");

int main() { return 0; }
