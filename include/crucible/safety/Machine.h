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

// Factory: construct Machine<State> by forwarding args to State's constructor.
template <typename State, typename... Args>
    requires std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr Machine<State> make_machine(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<State, Args...>)
{
    return Machine<State>{std::in_place, std::forward<Args>(args)...};
}

// Helper for the common transition pattern: consume old Machine,
// return new Machine with a constructed state.  Useful when the
// transition performs no additional work beyond state replacement.
template <typename NewState, typename OldState>
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
