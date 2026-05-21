// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type that stores a CrucibleContext::CompiledView
// as a non-static data member.  Fires the Tier-2 reflection audit
// `no_scoped_view_field_check`, which recursively walks the member's
// type tree and rejects any ScopedView found.
//
// CrucibleContext::CompiledView is the carrier-locked type alias for
// `crucible::fixy::wrap::ScopedView<CrucibleContext, ctx_mode::Compiled>`.
// Reflection sees through the alias and triggers the same audit that
// fires for the explicit ScopedView spelling — proof that the
// discipline is alias-stable across the CrucibleContext carrier.
//
// CrucibleContext itself opts in to this audit at CrucibleContext.h:497
// with
//     static_assert(crucible::fixy::wrap::no_scoped_view_field_check<CrucibleContext>());
// — that asserts CrucibleContext itself does NOT store a view.  This
// fixture asserts the AUDIT is also live for INDEPENDENT carrier
// types that try to embed a CrucibleContext::CompiledView.
//
// HS14 — paired with neg_crucible_context_mint_compiled_view_cross_carrier:
//   * That one catches typed-overload resolution at call time
//     (cross-carrier ScopedView mismatch).
//   * THIS one catches structural lifetime escape at field-storage time.
// Together they pin BOTH soundness gates of CrucibleContext::mint_compiled_view.

#include <crucible/CrucibleContext.h>

// Anchor `mint_compiled_view` in the source so the HS14 fixture
// scanner (rg "\bmint_compiled_view\b" across test/*_neg/) counts
// this file.  The anchor itself does not change the discipline being
// tested — the static_assert below is the actual neg-compile gate.
[[maybe_unused]] static auto anchor_compiled_view_mint() {
    crucible::CrucibleContext ctx;
    // mint_compiled_view's pre `mode_ == COMPILED` would fire on the
    // default-constructed (RECORD) ctx at runtime — but this fixture
    // is a compile-time test and runtime is never reached.  The
    // function-body-execution would also be suppressed by the file's
    // static_assert failure below.  The anchor exists solely for
    // grep-discoverability of the bare-name token.
    return ctx.mint_compiled_view();
}

// A class with a CrucibleContext::CompiledView field.  The audit walks
// `view_`'s declared type, sees a ScopedView<CrucibleContext, Compiled>
// hiding behind the typedef, and rejects the audit.  No constructor
// is declared — the audit reads layout from the declaration; the
// class need never be instantiated.
struct OffendingCtxContainer {
    crucible::CrucibleContext::CompiledView view_;
};

// Trigger the audit at compile time.  GCC's diagnostic names
// OffendingCtxContainer as the violator and ScopedView as the
// offending wrapper.
static_assert(::crucible::fixy::wrap::no_scoped_view_field_check<OffendingCtxContainer>(),
    "the audit must reject containers that store a CrucibleContext::"
    "CompiledView as a field; this fixture exists so a future "
    "regression in contains_scoped_view's recursive walk for the "
    "CrucibleContext carrier is caught at compile time.");

int main() { return 0; }
