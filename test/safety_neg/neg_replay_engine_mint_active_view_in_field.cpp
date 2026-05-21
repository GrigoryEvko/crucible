// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type that stores a ReplayEngine::ActiveView as a
// non-static data member.  Fires the Tier-2 reflection audit
// `no_scoped_view_field_check`, which recursively walks the member's
// type tree and rejects any ScopedView found.
//
// ReplayEngine::ActiveView is the carrier-locked type alias for
// `crucible::fixy::wrap::ScopedView<ReplayEngine, engine_state::Active>`.
// Reflection sees through the alias and triggers the same audit that
// fires for the explicit ScopedView spelling — proof that the
// discipline is alias-stable across the ReplayEngine carrier.
//
// HS14 — paired with neg_replay_engine_mint_active_view_cross_carrier:
//   * That one catches typed-overload resolution at call time
//     (cross-carrier ScopedView mismatch).
//   * THIS one catches structural lifetime escape at field-storage time.
// Together they pin BOTH soundness gates of ReplayEngine::mint_active_view.

#include <crucible/ReplayEngine.h>

// Anchor `mint_active_view` in the source so the HS14 fixture scanner
// (rg "\bmint_active_view\b" across test/*_neg/) counts this file.
// The anchor itself does not change the discipline being tested — the
// static_assert below is the actual neg-compile gate.
[[maybe_unused]] static auto anchor_active_view_mint() {
    crucible::ReplayEngine engine;
    // mint_active_view's pre `is_initialized()` would fire on the
    // default-constructed (ops_ == nullptr) engine at runtime — but
    // this fixture is a compile-time test and runtime is never
    // reached.  The function-body-execution would also be suppressed
    // by the file's static_assert failure below.  The anchor exists
    // solely for grep-discoverability of the bare-name token.
    return engine.mint_active_view();
}

// A class with a ReplayEngine::ActiveView field.  The audit walks
// `view_`'s declared type, sees a ScopedView<ReplayEngine, Active>
// hiding behind the typedef, and rejects the audit.  No constructor
// is declared — the audit reads layout from the declaration; the
// class need never be instantiated.
struct OffendingEngineContainer {
    crucible::ReplayEngine::ActiveView view_;
};

// Trigger the audit at compile time.  GCC's diagnostic names
// OffendingEngineContainer as the violator and ScopedView as the
// offending wrapper.
static_assert(::crucible::fixy::wrap::no_scoped_view_field_check<OffendingEngineContainer>(),
    "the audit must reject containers that store a ReplayEngine::"
    "ActiveView as a field; this fixture exists so a future "
    "regression in contains_scoped_view's recursive walk for the "
    "ReplayEngine carrier is caught at compile time.");

int main() { return 0; }
