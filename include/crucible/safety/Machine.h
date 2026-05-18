#pragma once

// ── crucible::safety::Machine<State> ────────────────────────────────
//
// Type-state lifecycle.  A Machine is a typed container for a state
// value; transitions are rvalue-qualified free functions that consume
// `Machine<StateA>&&` and return `Machine<StateB>`.
//
//   Axiom coverage: MemSafe, BorrowSafe, TypeSafe.
//   Runtime cost:   zero.  sizeof(Machine<S>) == sizeof(S).
//
// Difference from Session:
//   Session<...>   — linear protocol with fixed step sequence.
//   Machine<State> — general state machine with branching transitions.
//
// Example:
//   namespace conn {
//       struct Disconnected {};
//       struct Connecting { std::string host; uint32_t attempt; };
//       struct Connected  { SocketHandle sock; };
//
//       [[nodiscard]] Machine<Connecting> connect(
//           Machine<Disconnected>&& m, std::string host)
//       {
//           (void)std::move(m).extract();
//           return Machine<Connecting>{Connecting{std::move(host), 0}};
//       }
//   }
//
// Calling conn::connect on a Machine<Connecting> = compile error
// (overload resolution fails).

#include <crucible/Platform.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── machine_transition<From, To> — opt-in transition relation ─────
//
// fixy-H-21: until this trait was added, `transition_to<NewState>(
// Machine<OldState>&&, NewState)` accepted ANY state-pair, so a
// producer could write `transition_to<Disconnected>(authenticated)`
// and the type system silently allowed the rollback.  The substrate
// now requires an explicit per-state-pair opt-in: callers ship a
// specialization stating "this transition is legal", and the gate
// fires at the call site otherwise.
//
// Discipline:
//   * Primary defaults to std::false_type — unspecified pairs reject.
//   * Diagonal `machine_transition<S, S>` is true by default — same-
//     state moves replace the payload without changing the conceptual
//     state and are universally legal (production callers use them to
//     update counters / refresh fields).
//   * Production callers ship one `CRUCIBLE_ALLOW_MACHINE_TRANSITION(
//     From, To)` per allowed edge, ideally right next to the state
//     struct declarations so the transition graph is locally readable.
//
// The trait is a CLOSED data-flow boundary in the §XXI sense: every
// `transition_to` call's authority derives from one specialization
// hit.  A grep for `machine_transition<` enumerates the entire
// transition relation across the codebase.
template <typename From, typename To>
struct machine_transition : std::false_type {};

// Diagonal — same-state move is always allowed (payload update,
// not a conceptual transition).
template <typename S>
struct machine_transition<S, S> : std::true_type {};

template <typename From, typename To>
inline constexpr bool machine_transition_v =
    machine_transition<From, To>::value;

// MachineTransition<From, To> — concept gate for `transition_to`'s
// requires-clause.  Pure trait alias; lifts the bool into the concept
// namespace so the error message reads "MachineTransition<A, B> was
// not satisfied" rather than the noisier static_assert chain.
template <typename From, typename To>
concept MachineTransition = machine_transition_v<From, To>;

template <typename State>
class [[nodiscard]] Machine {
    State state_;

public:
    using state_type = State;

    // Move from State.
    constexpr explicit Machine(State s)
        noexcept(std::is_nothrow_move_constructible_v<State>)
        : state_{std::move(s)} {}

    // In-place construction of the State.
    template <typename... Args>
        requires std::is_constructible_v<State, Args...>
    constexpr explicit Machine(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<State, Args...>)
        : state_{std::forward<Args>(args)...} {}

    Machine(const Machine&)            = delete("Machine is move-only; transitions consume it");
    Machine& operator=(const Machine&) = delete("Machine is move-only; transitions consume it");
    Machine(Machine&&)                  = default;
    Machine& operator=(Machine&&)       = default;
    ~Machine()                          = default;

    // Borrow current state — for logging, UI rendering, inspection.
    // Does NOT consume the Machine.
    [[nodiscard]] constexpr const State& data() const & noexcept { return state_; }

    // Mutable borrow for in-place field updates.  Prefer transitions
    // where the change represents a conceptual state change; use this
    // only for within-state bookkeeping (e.g., incrementing a counter).
    [[nodiscard]] constexpr State& data_mut() & noexcept { return state_; }

    // Consume the Machine, yielding the inner state value.  The
    // transition function is expected to wrap this into a new Machine
    // of the next state type.
    [[nodiscard]] constexpr State extract() &&
        noexcept(std::is_nothrow_move_constructible_v<State>)
    {
        return std::move(state_);
    }
};

template <typename State>
Machine(State) -> Machine<State>;

// ── mint_machine<State>(args...) — Universal Mint Pattern ─────────
//
// Token mint per CLAUDE.md §XXI — constructs Machine<State> by
// forwarding args to State's constructor.  Authority derives from the
// constructibility proof; this is the canonical authorization point
// for entering the typed state machine (subsequent transitions go
// through transition_to_state, machine_transition, etc.).
template <typename State, typename... Args>
    requires std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr Machine<State> mint_machine(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<State, Args...>)
{
    return Machine<State>{std::in_place, std::forward<Args>(args)...};
}

// Helper for the common transition pattern: consume old Machine,
// return new Machine with a constructed state.  Useful when the
// transition performs no additional work beyond state replacement.
//
// fixy-H-21: requires-clause gates the transition on the opt-in
// `machine_transition<OldState, NewState>` relation.  Pairs without
// a specialization are rejected at the call site; the diagnostic
// names `MachineTransition<OldState, NewState>` so the failing edge
// is immediately readable.
template <typename NewState, typename OldState>
    requires MachineTransition<OldState, NewState>
[[nodiscard]] constexpr Machine<NewState> transition_to(
    Machine<OldState>&& m, NewState s)
    noexcept(std::is_nothrow_move_constructible_v<NewState>
             && std::is_nothrow_move_constructible_v<OldState>)
{
    (void)std::move(m).extract();
    return Machine<NewState>{std::move(s)};
}

// Zero-cost guarantee.
static_assert(sizeof(Machine<int>)   == sizeof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*));

} // namespace crucible::safety

// CRUCIBLE_ALLOW_MACHINE_TRANSITION(From, To) — convenience for
// adjacent declarations to the state structs.  Expands to the
// canonical `machine_transition<From, To>` specialization.  Must
// appear at namespace scope.
#define CRUCIBLE_ALLOW_MACHINE_TRANSITION(From, To)                       \
    namespace crucible::safety {                                          \
        template <> struct machine_transition<From, To> : std::true_type{};\
    }
