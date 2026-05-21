// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `transition_to<NewState>(Machine<OldState>&&,
// NewState)` for an (OldState, NewState) pair that has NO opt-in
// specialization of `machine_transition<From, To>`.  The helper's
// requires-clause `MachineTransition<OldState, NewState>` rejects at
// overload resolution.
//
// This is the discipline added in fixy-H-21: prior to that fix, the
// helper accepted ANY state-pair so a producer could write
// `transition_to<Disconnected>(authenticated)` and the type system
// silently allowed the rollback.  Per CLAUDE.md §XVI Machine entry
// and the trait's doc-block:
//
//   * Primary `machine_transition<From, To>` defaults to false_type.
//   * Diagonal `<S, S>` is true (same-state payload refresh).
//   * Production callers ship one CRUCIBLE_ALLOW_MACHINE_TRANSITION
//     per allowed edge.  This fixture deliberately omits the opt-in,
//     so the call MUST fail.
//
// HS14 — paired with neg_machine_copy_rejected for distinct mismatch
// classes (Class T deleted-function copy vs Class U concept-gate
// transition).  Both classes together pin Machine<>'s soundness.
//
// This file is U-140's Class U fixture — the transition opt-in gate.

#include <crucible/safety/Machine.h>

namespace {
    struct Disconnected {};
    struct Connected    {};
    // NB: deliberately NO CRUCIBLE_ALLOW_MACHINE_TRANSITION(Disconnected,
    // Connected) — the absence is the violation we're witnessing.
}

[[maybe_unused]] static auto anchor_machine_mint() {
    return ::crucible::safety::mint_machine<Disconnected>();
}

// VIOLATION: transition_to<Connected> on a Machine<Disconnected>
// requires `MachineTransition<Disconnected, Connected>`, which is
// false (no specialization opted in).  GCC rejects at the requires-
// clause with "constraints not satisfied" / "concept ...
// MachineTransition" not satisfied.
[[maybe_unused]] static auto offending_transition() {
    auto m = ::crucible::safety::mint_machine<Disconnected>();
    return ::crucible::safety::transition_to<Connected>(
        std::move(m), Connected{});                   // ERROR: not opted in
}

int main() { return 0; }
