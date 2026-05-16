#pragma once

// ── crucible::fixy — Mach.h (FIXY-C, alias re-export of safety/Machine.h) ─
//
// Stable surface for typestate state machines.  `safety::Machine<State>`
// is the substrate; `fixy::mach::Machine<State>` is the discipline-
// surface alias.  No new types; the `using` declarations are inert.
//
// Difference from Session (and from fixy::sess::*): Machine is a
// GENERAL state machine with branching transitions (any state to any
// state, transitions chosen at runtime within the type-system's
// allowed set); Session is a LINEAR protocol with a fixed step
// sequence.  Both are typestate disciplines, but the use sites differ:
//
//   - fixy::sess::*       — wire protocols, IO handshakes, channel
//                            lifecycles where the order is fixed.
//   - fixy::mach::*       — Vigil mode, CipherTier promotion,
//                            connection retry FSM where transitions
//                            branch by runtime input.
//
// Both compose: a Session<...> handle's state may carry a Machine<S>,
// and a Machine<S>'s transition body may consume a Session.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   namespace fixy::mach {
//     using Machine;          // safety::Machine<State> — move-only typestate
//     using mint_machine;     // safety::mint_machine — Universal Mint factory
//     using transition_to;    // safety::transition_to<NewState>(Machine<Old>&&, NewState)
//   }
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase C — Mach.h deliverable
//   safety/Machine.h                    — Machine<State> typestate primitive
//   CLAUDE.md §XXI                      — Universal Mint Pattern

#include <crucible/safety/Machine.h>

namespace crucible::fixy::mach {

using ::crucible::safety::Machine;
using ::crucible::safety::mint_machine;
using ::crucible::safety::transition_to;

// ── Self-test — identity check ─────────────────────────────────────
//
// Compiles iff the alias path resolves to the substrate type.  A
// future move/rename of safety::Machine<...> breaks here first
// (rather than at every downstream consumer site).

namespace self_test {

static_assert(std::is_same_v<Machine<int>,
                             ::crucible::safety::Machine<int>>,
    "fixy::mach::Machine alias must be identical to safety::Machine.");

// Zero-cost claim: the alias must NOT add a byte over the underlying
// state type.  Mirrors the substrate's own assertion.
static_assert(sizeof(Machine<int>)   == sizeof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*));

}  // namespace self_test

}  // namespace crucible::fixy::mach
