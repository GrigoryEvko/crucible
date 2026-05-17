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

#include <type_traits>
#include <utility>

namespace crucible::fixy::mach {

using ::crucible::safety::Machine;
using ::crucible::safety::mint_machine;
using ::crucible::safety::transition_to;

// ═════════════════════════════════════════════════════════════════════
// ── Transition-validation helpers (FIXY-AUDIT-C1) ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The substrate's Machine.h exposes only `state_type` plus the two
// minters (`mint_machine`, `transition_to`).  Production callers
// frequently need to:
//
//   1. Project the state type out of an arbitrary `Machine<S>` value
//      without a verbose `typename decltype(m)::state_type` spelling.
//   2. Verify at compile time that a `transition_to<NewState>(m, s)`
//      call would succeed for a given Machine type and target state,
//      so a static_assert (or `requires`-clause) can pin the transition
//      contract at a call site rather than waiting for an overload-
//      resolution diagnostic.
//
// Both helpers are pure metafunctions over the substrate's existing
// shape; no new state path is introduced.  See task #1425.

// state_of_t<M> — project state_type out of any Machine<S> reference.
//
//   state_of_t<Machine<int>&>          == int
//   state_of_t<Machine<Connecting>&&>  == Connecting
//
// The cv-ref strip mirrors the substrate's overload-resolution
// behavior — transitions take `Machine<S>&&`, so the relevant type is
// always the unqualified `Machine<S>`'s `state_type`.
template <typename M>
using state_of_t = typename std::remove_cvref_t<M>::state_type;

// is_machine_v<T> — recognizer for `Machine<S>` specializations.
//
// True when `T` (after cv-ref strip) is `Machine<S>` for some S.  Used
// inside `can_transition_v`'s gate so the helper rejects non-Machine
// arguments cleanly rather than producing a substitution failure deep
// inside `state_of_t`.

namespace detail {

template <typename T>
struct is_machine_impl : std::false_type {};

template <typename S>
struct is_machine_impl<::crucible::safety::Machine<S>> : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool is_machine_v =
    detail::is_machine_impl<std::remove_cvref_t<T>>::value;

// can_transition_v<M, NewState> — would `transition_to<NewState>(m, s)`
// compile for a Machine of type M and a target state NewState?
//
// The substrate's `transition_to<NewState>(Machine<OldState>&&, NewState)`
// is well-formed iff:
//   1. M is a Machine<OldState> for some OldState (rejected otherwise).
//   2. OldState is move-constructible (so the consumed Machine can
//      yield its state via `.extract()`).
//   3. NewState is move-constructible (so the returned Machine<NewState>
//      can wrap the supplied state value).
//
// can_transition_v folds those three structural facts.  Callers spell
//
//   static_assert(fixy::mach::can_transition_v<Machine<A>, B>);
//
// to pin the transition contract at compile time without invoking
// `transition_to` itself.
//
// Implementation note: `state_of_t<M>` would substitution-fail when M
// is not a Machine, so the lookup is staged behind `is_machine_v` via a
// partial specialization.  C++ `&&` does not short-circuit template
// substitution.

namespace detail {

template <typename M, typename NewState, bool IsMachine>
struct can_transition_impl : std::false_type {};

template <typename M, typename NewState>
struct can_transition_impl<M, NewState, true>
    : std::bool_constant<
          std::is_move_constructible_v<
              typename std::remove_cvref_t<M>::state_type>
       && std::is_move_constructible_v<NewState>> {};

}  // namespace detail

template <typename M, typename NewState>
inline constexpr bool can_transition_v =
    detail::can_transition_impl<M, NewState, is_machine_v<M>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── LTL temporal operators (FIXY-AUDIT-B8) — SUBSTRATE GAP ─────────
// ═════════════════════════════════════════════════════════════════════
//
// The misc/16_05_2026_fixy.md design doc considered surfacing linear
// temporal logic (LTL) operators on Machine state predicates so callers
// could write specifications like
//
//     static_assert(eventually_v<Machine<Disconnected>, Connected>);
//     static_assert(globally_v<Machine<Authenticated>, has_session_token>);
//     static_assert(until_v<Machine<Pending>, has_credit, Approved>);
//
// at compile time.  The substrate `safety/Machine.h` ships NO such
// operators today — it exposes only the value-typed carrier plus the
// `mint_machine` / `transition_to` token mints.  The `can_transition_v`
// helper above is the closest analogue: it captures the "next-state
// is reachable" structural fact, which is the LTL Next (X) operator
// degraded to a single-step type-system predicate.
//
// ── Operator-by-operator status ────────────────────────────────────
//
//   X p  (Next)         partial — `can_transition_v<M, NewState>`
//                       projects the substrate's overload-resolution
//                       gate.  Predicate `p` is NOT carried; callers
//                       stack a downstream predicate (e.g., a Refined
//                       wrapper on the NewState's fields) manually.
//
//   F p  (Eventually)   absent — would require a reachability fold
//                       over the transition relation, which is not a
//                       value-typed property of Machine<State>.  The
//                       substrate has no transition relation table;
//                       transitions are scattered free functions
//                       discovered via overload resolution.
//
//   G p  (Globally)     absent — would require enumerating every
//                       reachable state, which presupposes the
//                       Eventually fold above.
//
//   p U q (Until)       absent — composition of Globally and
//                       Eventually; depends on both being expressible.
//
//   p R q (Release)     absent — dual of Until.
//
//   p S q (Since)       absent — past-time dual of Until.
//
// ── Workaround for downstream code ─────────────────────────────────
//
// Production callers express the LTL fragments they need via chains
// of `can_transition_v` plus Refined / typestate wrappers on each
// state's payload:
//
//   // "From Pending, eventually we reach Approved" — encode the
//   //  single intermediate step explicitly:
//   static_assert(can_transition_v<Machine<Pending>,     Approving>);
//   static_assert(can_transition_v<Machine<Approving>,   Approved>);
//
//   // "In Approved, always has_session_token" — encode via Refined<>
//   //  on the Approved state's payload, not via a Globally fold.
//
// The chain-encoding is verbose but structural: every transition is
// already discoverable via overload resolution, and every per-state
// invariant is already discoverable via Refined<>.  LTL would add
// terseness, not new expressive power, until a substrate transition
// table lands.
//
// ── Deferred work ──────────────────────────────────────────────────
//
// LTL operator support is deferred until the substrate ships a value-
// typed transition table on Machine<State> (a `transition_map_v<State>`
// non-type template parameter, an explicit `transitions_to<...>`
// trait, or similar).  Once that lands, `fixy::mach::ltl::next_v`,
// `eventually_v`, `globally_v`, `until_v`, `release_v`, `since_v`
// would re-export the substrate primitives under the LTL spelling.
// No such substrate facility exists today; the absence is structural,
// not an oversight in the re-export layer.
//
// Downstream code MUST NOT roll its own LTL fold over `can_transition_v`
// chains expecting the substrate to grow the table later — the
// substrate is deliberately scattered, and the existing predicate
// composition (Refined per state + `can_transition_v` per step) is
// the canonical pattern.

}  // namespace crucible::fixy::mach
