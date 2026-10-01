#pragma once

// A type-indexed state machine: the state is the type, and the legal
// transitions are a relation over state types.
//
// The relation is opt-in: an edge exists only where a declaration says
// so, so a grep for the edge type enumerates every legal transition in
// the codebase.  Without it a caller could roll a machine back to an
// earlier state and the type system would allow it.  Declare each edge
// beside the state structs it joins, so the transition graph reads
// locally.
//
// The relation is a namespace of foundation::fail_closed::edge
// variables, read through Admitted, so its pairs are exactly the edges
// declared in the namespace the machine names.  A trait with one
// specialization for each edge would be open: any translation unit that
// sees the primary can add a pair.  Each machine names its own
// namespace as the Edges parameter.  A machine that names none reads
// the shared fixy::machine::admitted_transitions, which the macro at the
// foot of this file appends to.

#include <foundation/Platform.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/diag/RowHash.h>
#include <foundation/reflect/Instance.h>

#include <cstdlib>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// The shared relation.  An edge declared here is admitted by every
// machine that names no relation of its own.
namespace machine::admitted_transitions {}

// A same-state move replaces the payload without changing the
// conceptual state, so the diagonal needs no declaration.
template <typename From, typename To, std::meta::info Edges = ^^machine::admitted_transitions>
inline constexpr bool machine_transition_v =
    std::is_same_v<From, To> || ::foundation::fail_closed::Admitted<Edges, From, To>;

// A concept rather than the bare trait, so a rejected edge is reported
// as one unsatisfied constraint naming both states and the relation.
template <typename From, typename To, std::meta::info Edges = ^^machine::admitted_transitions>
concept MachineTransition = machine_transition_v<From, To, Edges>;

template <typename State, std::meta::info Edges = ^^machine::admitted_transitions>
class Machine;

// The one way into the machine.  Every later state is reached through
// the transition relation.
template <typename State, std::meta::info Edges = ^^machine::admitted_transitions, typename... Args>
    requires std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr Machine<State, Edges>
mint_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>);

template <typename NewState, typename OldState, std::meta::info Edges>
    requires MachineTransition<OldState, NewState, Edges>
[[nodiscard]] constexpr Machine<NewState, Edges>
transition_to(Machine<OldState, Edges>&& m, NewState s) noexcept(std::is_nothrow_move_constructible_v<NewState>
                                                                 && std::is_nothrow_move_constructible_v<OldState>);

template <typename State, std::meta::info Edges>
class [[nodiscard]] Machine {
    static_assert(std::meta::is_namespace(Edges), "Machine<State, Edges>: Edges must be the reflection of the "
                                                  "namespace that declares the machine's edges, written ^^name.");

    State state_;

    constexpr explicit Machine(State s) noexcept(std::is_nothrow_move_constructible_v<State>) : state_{std::move(s)} {}

    template <typename... Args>
        requires std::is_constructible_v<State, Args...>
    constexpr explicit Machine(std::in_place_t,
                               Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
        : state_{std::forward<Args>(args)...} {}

    // The two doors.  The mint builds the initial state, and the
    // transition builds every later one after the relation admits it.
    template <typename S, std::meta::info E, typename... Args>
        requires std::is_constructible_v<S, Args...>
    friend constexpr Machine<S, E> mint_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<S, Args...>);

    template <typename NewState, typename OldState, std::meta::info E>
        requires MachineTransition<OldState, NewState, E>
    friend constexpr Machine<NewState, E>
    transition_to(Machine<OldState, E>&& m, NewState s) noexcept(std::is_nothrow_move_constructible_v<NewState>
                                                                 && std::is_nothrow_move_constructible_v<OldState>);

public:
    using state_type = State;
    static constexpr std::meta::info edges = Edges;
    // The state is the grade and the edges are the relation, so the
    // machine names itself.  Nothing it holds is a payload.
    using row_discipline = Machine;
    using row_payload = ::foundation::diag::row_payloads<>;

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

template <typename State, std::meta::info Edges, typename... Args>
    requires std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr Machine<State, Edges>
mint_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>) {
    return Machine<State, Edges>{std::in_place, std::forward<Args>(args)...};
}

template <typename NewState, typename OldState, std::meta::info Edges>
    requires MachineTransition<OldState, NewState, Edges>
[[nodiscard]] constexpr Machine<NewState, Edges>
transition_to(Machine<OldState, Edges>&& m, NewState s) noexcept(std::is_nothrow_move_constructible_v<NewState>
                                                                 && std::is_nothrow_move_constructible_v<OldState>) {
    (void)std::move(m).extract();
    return Machine<NewState, Edges>{std::move(s)};
}

// Three queries over a machine type.

namespace mach {

template <typename M>
using state_of_t = typename std::remove_cvref_t<M>::state_type;

template <typename T>
inline constexpr bool is_machine_v = ::foundation::reflect::IsInstanceOf<T, ^^Machine>;

// The state_type lookup is staged behind an is_machine_v bool parameter
// rather than written as one conjunction: `&&` does not short-circuit
// template substitution, so a non-Machine M would hard-error inside the
// member-typedef lookup instead of yielding false.

namespace detail {

template <typename M, typename NewState, bool IsMachine>
struct can_transition_impl : std::false_type {};

template <typename M, typename NewState>
struct can_transition_impl<M, NewState, true>
    : std::bool_constant<std::is_move_constructible_v<typename std::remove_cvref_t<M>::state_type>
                         && std::is_move_constructible_v<NewState>
                         && machine_transition_v<typename std::remove_cvref_t<M>::state_type, NewState,
                                                 std::remove_cvref_t<M>::edges>> {};

}  // namespace detail

template <typename M, typename NewState>
inline constexpr bool can_transition_v = detail::can_transition_impl<M, NewState, is_machine_v<M>>::value;

}  // namespace mach

}  // namespace fixy

// Appends one edge to the shared relation.  Opens a namespace, so it
// must appear at namespace scope, and From and To are looked up from
// inside fixy::machine::admitted_transitions, so a state declared in a
// named namespace is written with its qualification.  The variable has
// internal linkage, so two translation units that admit different
// edges under the same counter value do not collide.  The relation is read the
// first time a pair is checked against it, so every edge comes before
// the first transition in the translation unit.
#define CRUCIBLE_MACHINE_EDGE_CAT2_(a, b) a##b
#define CRUCIBLE_MACHINE_EDGE_CAT_(a, b) CRUCIBLE_MACHINE_EDGE_CAT2_(a, b)
#define CRUCIBLE_ADMIT_MACHINE_TRANSITION(From, To)                                                               \
    namespace fixy::machine::admitted_transitions {                                                               \
    constexpr ::foundation::fail_closed::edge<From, To> CRUCIBLE_MACHINE_EDGE_CAT_(machine_edge_, __COUNTER__){}; \
    }
