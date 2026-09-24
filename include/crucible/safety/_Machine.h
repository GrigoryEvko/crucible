#pragma once

#include <crucible/Platform.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

// The relation is opt-in: an edge exists only where a specialization says so,
// so a grep for the trait enumerates every legal transition in the codebase.
// Without it a caller could roll a machine back to an earlier state and the
// type system would allow it.  Declare each edge beside the state structs it
// joins, so the transition graph reads locally.
template <typename From, typename To>
struct machine_transition : std::false_type {};

// A same-state move replaces the payload without changing the conceptual state,
// so the diagonal needs no declaration.
template <typename S>
struct machine_transition<S, S> : std::true_type {};

template <typename From, typename To>
inline constexpr bool machine_transition_v = machine_transition<From, To>::value;

// A concept rather than the bare trait, so a rejected edge is reported as one
// unsatisfied constraint naming both states.
template <typename From, typename To>
concept MachineTransition = machine_transition_v<From, To>;

template <typename State>
class [[nodiscard]] Machine {
    State state_;

public:
    using state_type = State;

    constexpr explicit Machine(State s) noexcept(std::is_nothrow_move_constructible_v<State>) : state_{std::move(s)} {}

    template <typename... Args>
        requires std::is_constructible_v<State, Args...>
    constexpr explicit Machine(std::in_place_t,
                               Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
        : state_{std::forward<Args>(args)...} {}

    Machine(const Machine&) = delete("Machine is move-only; transitions consume it");
    Machine& operator=(const Machine&) = delete("Machine is move-only; transitions consume it");
    Machine(Machine&&) = default;
    Machine& operator=(Machine&&) = default;
    ~Machine() = default;

    [[nodiscard]] constexpr const State& data() const& noexcept { return state_; }

    // For bookkeeping within one state only.  A change that means the machine
    // has entered another state goes through a transition instead.
    [[nodiscard]] constexpr State& data_mut() & noexcept { return state_; }

    [[nodiscard]] constexpr State extract() && noexcept(std::is_nothrow_move_constructible_v<State>) {
        return std::move(state_);
    }
};

template <typename State>
Machine(State) -> Machine<State>;

// The one way into the machine.  Every later state is reached through the
// transition relation.
template <typename State, typename... Args>
    requires std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr Machine<State>
mint_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>) {
    return Machine<State>{std::in_place, std::forward<Args>(args)...};
}

template <typename NewState, typename OldState>
    requires MachineTransition<OldState, NewState>
[[nodiscard]] constexpr Machine<NewState>
transition_to(Machine<OldState>&& m, NewState s) noexcept(std::is_nothrow_move_constructible_v<NewState>
                                                          && std::is_nothrow_move_constructible_v<OldState>) {
    (void)std::move(m).extract();
    return Machine<NewState>{std::move(s)};
}

static_assert(sizeof(Machine<int>) == sizeof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*));

}  // namespace crucible::safety

// Opens a namespace, so it must appear at namespace scope.
#define CRUCIBLE_ALLOW_MACHINE_TRANSITION(From, To)          \
    namespace crucible::safety {                             \
    template <>                                              \
    struct machine_transition<From, To> : std::true_type {}; \
    }
