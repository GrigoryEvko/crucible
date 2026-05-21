// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copying a Machine<State>.  Machine is move-only (per
// CLAUDE.md §XVI structural-wrappers list) — transitions consume the
// rvalue and the copy ctor / copy-assign carry a `delete("reason")`
// directive so the compiler diagnostic names the discipline reason.
//
// HS14 — paired with neg_machine_transition_without_optin for distinct
// mismatch classes (Class T: deleted-function rejection at copy-attempt
// time vs Class U: concept-gate rejection at transition-attempt time).
// Together the pair pins both soundness layers of safety::Machine<>:
//   (a) linear ownership (copies forbidden); and
//   (b) opt-in transition relation (machine_transition<From,To>).
//
// This file is U-140's Class T fixture — the linearity gate.  Closes
// the "Machine: 0 fixtures" entry surfaced by the previous /loop
// firing's neg-compile primitive-coverage audit.

#include <crucible/safety/Machine.h>

namespace {
    struct ConnState { int attempt = 0; };
}

// Mint a Machine — the legitimate entry point.  This call compiles.
[[maybe_unused]] static auto anchor_machine_mint() {
    return ::crucible::safety::mint_machine<ConnState>(0);
}

// VIOLATION: copy-construct a Machine.  Machine(const Machine&) is
// declared `delete("Machine is move-only; transitions consume it")`,
// so the compiler emits a use-of-deleted-function diagnostic carrying
// that reason string.
[[maybe_unused]] static auto offending_machine_copy() {
    auto m1 = ::crucible::safety::mint_machine<ConnState>(0);
    ::crucible::safety::Machine<ConnState> m2 = m1;  // ERROR: copy is deleted
    return m2;
}

int main() { return 0; }
