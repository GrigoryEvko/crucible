// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing the result of PoolAllocator::mint_initialized_view()
// to a helper that expects a CKernelTable::MutableView.  Both are
// ScopedView<Carrier, Tag> instantiations but they parametrize on
// DIFFERENT Carriers (PoolAllocator vs CKernelTable) — the type system
// rejects the conversion at overload resolution before runtime is
// reached.
//
// PoolAllocator has only one typed view (InitializedView; no companion
// "DestroyedView") so the intra-carrier per-state mismatch pattern used
// for CKernelTable/SchemaTable (U-135/U-136) doesn't apply.  The
// cross-carrier mismatch is the equivalent type-system gate, mirroring
// U-137's CrucibleContext::mint_compiled_view fixture.
//
// HS14 — paired with neg_pool_allocator_mint_initialized_view_in_field
// for distinct mismatch classes (value-level call-time vs structural
// field-storage).

#include <crucible/PoolAllocator.h>
#include <crucible/CKernel.h>

// A fixture-local helper that consumes a CKernelTable::MutableView.
// The point is not the CKernelTable role — it's that this is a
// DIFFERENT ScopedView<Carrier, Tag> instantiation, so the conversion
// from InitializedView must fail.
static void requires_ckernel_mutable_view(
    crucible::CKernelTable::MutableView const&) noexcept {}

int main() {
    crucible::PoolAllocator pool;
    // Default-constructed PoolAllocator has ptr_table_ == nullptr, so
    // mint_initialized_view()'s `pre(is_initialized())` would fire at
    // runtime — but this neg-compile fixture never gets to runtime.
    // GCC type-checks the requires_ckernel_mutable_view call below
    // and rejects the cross-carrier conversion at parse time.
    auto iv = pool.mint_initialized_view();
    requires_ckernel_mutable_view(iv);  // ERROR: cross-carrier mismatch
    return 0;
}
