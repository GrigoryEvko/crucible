// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing the result of ReplayEngine::mint_active_view() to
// a helper that expects a CKernelTable::MutableView.  Both are
// ScopedView<Carrier, Tag> instantiations but they parametrize on
// DIFFERENT Carriers (ReplayEngine vs CKernelTable) — the type system
// rejects the conversion at overload resolution before runtime is
// reached.
//
// ReplayEngine has only one typed view (ActiveView; no companion
// "InactiveView" or "DivergedView") so the intra-carrier per-state
// mismatch pattern used for CKernelTable/SchemaTable (U-135/U-136)
// doesn't apply.  The cross-carrier mismatch is the equivalent type-
// system gate, mirroring the U-137 (CrucibleContext) and U-138
// (PoolAllocator) fixtures.
//
// HS14 — paired with neg_replay_engine_mint_active_view_in_field for
// distinct mismatch classes (value-level call-time vs structural
// field-storage).

#include <crucible/ReplayEngine.h>
#include <crucible/CKernel.h>

// A fixture-local helper that consumes a CKernelTable::MutableView.
// The point is not the CKernelTable role — it's that this is a
// DIFFERENT ScopedView<Carrier, Tag> instantiation, so the conversion
// from ActiveView must fail.
static void requires_ckernel_mutable_view(
    crucible::CKernelTable::MutableView const&) noexcept {}

int main() {
    crucible::ReplayEngine engine;
    // Default-constructed ReplayEngine has ops_ == nullptr, so
    // mint_active_view()'s `pre(is_initialized())` would fire at
    // runtime — but this neg-compile fixture never gets to runtime.
    // GCC type-checks the requires_ckernel_mutable_view call below
    // and rejects the cross-carrier conversion at parse time.
    auto av = engine.mint_active_view();
    requires_ckernel_mutable_view(av);  // ERROR: cross-carrier mismatch
    return 0;
}
