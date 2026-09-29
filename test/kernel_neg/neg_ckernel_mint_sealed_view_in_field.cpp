// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class type that keeps a CKernelTable::SealedView as a non-static data
// member must fail the reflection audit no_scoped_view_field_check.  A
// sealed view proves that the table was sealed when the view was minted.
// A field keeps that proof after the table is gone or opened again.
//
// The companion fixture neg_ckernel_register_with_sealed_view catches a
// sealed view at a call that wants the mutable one.  This fixture catches
// a sealed view that a field keeps.
//
// Expected diagnostic: the static assertion of the audit fails for
// SealedContainer.

#include <crucible/CKernel.h>
#include <fixy/ScopedView.h>

[[maybe_unused]] static auto anchor_sealed_view_mint() {
    crucible::CKernelTable table;
    table.seal();
    return table.mint_sealed_view();
}

// The audit reads the layout from the declaration, so the class needs no
// constructor and no instance.
struct SealedContainer {
    crucible::CKernelTable::SealedView view_;
};

static_assert(::fixy::no_scoped_view_field_check<SealedContainer>(),
              "the audit must reject a class that keeps a CKernelTable::SealedView as a field");

int main() { return 0; }
