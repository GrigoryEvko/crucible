// Negative-compile test.  This file must fail to compile.
//
// Violation: passing the result of ReplayEngine::mint_active_view() to
// a helper that expects a CKernelTable::MutableView.  Both are
// ScopedView<Carrier, Tag> instantiations but they parametrize on
// different Carriers (ReplayEngine vs CKernelTable) — the type system
// rejects the conversion at overload resolution before runtime is
// reached.
//
// ReplayEngine has only one typed view (ActiveView; no companion
// "InactiveView" or "DivergedView") so the intra-carrier per-state
// mismatch pattern used for CKernelTable/SchemaTable doesn't apply.
// The cross-carrier mismatch is the equivalent type-system gate,
// mirroring the CrucibleContext and PoolAllocator cross-carrier
// fixtures.
//
// HS14 — paired with neg_replay_engine_mint_active_view_in_field for
// distinct mismatch classes (value-level call-time vs structural
// field-storage).

#include <crucible/CrucibleContext.h>
#include <crucible/CKernel.h>

// A fixture-local helper that consumes a CKernelTable::MutableView.
// The point is not the CKernelTable role — it's that this is a
// different ScopedView<Carrier, Tag> instantiation, so the conversion
// from ActiveView must fail.
static void requires_ckernel_mutable_view(crucible::CKernelTable::MutableView const&) noexcept {}

int main() {
    crucible::CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    // The context records, so its engine has no region, and the precondition
    // of mint_active_view would end the process at run time.  The fixture does
    // not run: the call of requires_ckernel_mutable_view does not compile.
    auto av = ctx.engine().mint_active_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    requires_ckernel_mutable_view(av);  // Error: cross-carrier mismatch
    return 0;
}
