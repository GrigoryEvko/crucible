// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing the result of CrucibleContext::mint_compiled_view()
// to a helper that expects a CKernelTable::MutableView.  The two views
// share the underlying `ScopedView<Carrier, Tag>` shape but parametrize
// it on DIFFERENT Carriers (CrucibleContext vs CKernelTable) — the
// type system rejects the conversion at overload resolution.
//
// CrucibleContext has only one typed view (CompiledView; no companion
// "RecordingView") so the intra-carrier per-state mismatch pattern used
// for CKernelTable/SchemaTable (U-135/U-136) doesn't apply here.  The
// cross-carrier mismatch is the equivalent type-system gate: the type
// system distinguishes carriers AND states, both layers of the lock
// must reject independently.
//
// The mint_compiled_view's pre `mode_ == ContextMode::COMPILED` would
// fire at runtime on a default-constructed context, but this fixture
// is a COMPILE-time test — we never reach the runtime pre.  GCC
// rejects at overload resolution before the body of main() can ever
// execute.
//
// HS14 — paired with neg_crucible_context_mint_compiled_view_in_field
// for distinct mismatch classes (value-level call-time vs structural
// field-storage).

#include <crucible/CrucibleContext.h>
#include <crucible/CKernel.h>

// A fixture-local helper that consumes a CKernelTable::MutableView.
// The point is not the CKernelTable role — it's that this is a
// DIFFERENT ScopedView<Carrier, Tag> instantiation, so the conversion
// from CompiledView must fail.
static void requires_ckernel_mutable_view(
    crucible::CKernelTable::MutableView const&) noexcept {}

int main() {
    crucible::CrucibleContext ctx;
    // Default-constructed CrucibleContext is in RECORD mode; calling
    // mint_compiled_view() would fire its `pre(mode_ == COMPILED)` at
    // runtime — but this neg-compile fixture never gets to runtime.
    // GCC type-checks the requires_ckernel_mutable_view call below
    // and rejects the cross-carrier conversion at parse time.
    auto cv = ctx.mint_compiled_view();
    requires_ckernel_mutable_view(cv);  // ERROR: cross-carrier mismatch
    return 0;
}
