#pragma once

// ── crucible::fixy::mach — Machine minter under fixy:: ─────────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the
// safety/Machine.h state-machine carrier and its mint factory under
// `fixy::mach::` so callers who include only the fixy umbrella never
// have to descend into the safety/ tree to mint a typed state
// machine.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: the re-export preserves
// the substrate's `std::is_constructible_v<State, Args...>` token-
// mint gate, the `[[nodiscard]] constexpr noexcept(...)` qualifiers,
// and the in-place-construction discipline.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::Machine<State>             — typed state-machine carrier
//   safety::mint_machine<State>(args)  — token-mint factory
//   safety::transition_to<NewState>(m, ns)  — transition helper
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — substrate forwards args into State's constructor;
//              re-export preserves.
//   TypeSafe — using-declarations preserve the substrate's
//              std::is_constructible gate.
//   MemSafe  — Machine is value-typed; no heap.
//   DetSafe  — pure value construction; bit-exact.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/safety/Machine.h>

namespace crucible::fixy::mach {

using ::crucible::safety::Machine;
using ::crucible::safety::mint_machine;
using ::crucible::safety::transition_to;

}  // namespace crucible::fixy::mach
