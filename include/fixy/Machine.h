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
// The old header stated the relation as a trait, machine_transition
// <From, To>, with one specialization per edge.  A trait is open: any
// translation unit that sees the primary can add a pair.  The relation
// is now a namespace of foundation::fail_closed::edge variables, read
// through Admitted, so its pairs are exactly the edges declared in the
// namespace the machine names.  Each machine names its own namespace as
// the Edges parameter; a machine that names none reads the shared
// fixy::machine::admitted_transitions, which the macro at the foot of
// this file appends to.
//
// Old spelling: include/crucible/safety/Machine.h and the three
// helpers of include/crucible/fixy/Mach.h.

#include <foundation/Platform.h>
#include <foundation/diag/FailClosed.h>
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

static_assert(sizeof(Machine<int>) == sizeof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*));

// The three helpers of the old fixy/Mach.h.

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

namespace detail::machine_self_test {

struct Disconnected {};
struct Connecting {
    int attempt = 0;
    constexpr explicit Connecting(int a) noexcept : attempt{a} {}
};
struct Connected {
    int fd = -1;
    constexpr explicit Connected(int f) noexcept : fd{f} {}
};

// A per-machine relation: two edges, forward only.
namespace connection_edges {
inline constexpr ::foundation::fail_closed::edge<Disconnected, Connecting> disconnected_to_connecting{};
inline constexpr ::foundation::fail_closed::edge<Connecting, Connected> connecting_to_connected{};
}  // namespace connection_edges

using ConnMachine = Machine<Disconnected, ^^connection_edges>;

static_assert(MachineTransition<Disconnected, Connecting, ^^connection_edges>);
static_assert(MachineTransition<Connecting, Connected, ^^connection_edges>);
static_assert(MachineTransition<Connected, Connected, ^^connection_edges>, "the diagonal needs no declaration");
static_assert(!MachineTransition<Connecting, Disconnected, ^^connection_edges>, "the relation is one-way");
static_assert(!MachineTransition<Disconnected, Connected, ^^connection_edges>, "no edge skips a state");
static_assert(!MachineTransition<Disconnected, Connecting>, "an edge in one relation is not in the shared one");

static_assert(std::is_same_v<mach::state_of_t<ConnMachine>, Disconnected>);
static_assert(std::is_same_v<mach::state_of_t<Machine<int>&>, int>, "state_of_t<Machine<int>&> must project to int");
static_assert(std::is_same_v<mach::state_of_t<Machine<Connected, ^^connection_edges>&&>, Connected>);

static_assert(mach::is_machine_v<Machine<int>>);
static_assert(mach::is_machine_v<Machine<int>&>);
static_assert(mach::is_machine_v<ConnMachine const&>);
static_assert(!mach::is_machine_v<int>);
static_assert(!mach::is_machine_v<void>);
static_assert(!mach::is_machine_v<Disconnected>);

static_assert(mach::can_transition_v<ConnMachine, Connecting>);
static_assert(mach::can_transition_v<Machine<Connecting, ^^connection_edges>, Connected>);
static_assert(!mach::can_transition_v<ConnMachine, Connected>);
static_assert(!mach::can_transition_v<int, double>,
              "can_transition_v must reject non-Machine M without substitution-failing.");
static_assert(!mach::can_transition_v<Disconnected, Connecting>);

static_assert(!std::is_copy_constructible_v<ConnMachine>);
static_assert(std::is_move_constructible_v<ConnMachine>);
static_assert(!std::is_constructible_v<ConnMachine, Disconnected>,
              "the constructor is private; mint_machine is the door");

[[nodiscard]] consteval bool walks_the_relation() noexcept {
    auto m_disc = mint_machine<Disconnected, ^^connection_edges>();
    auto m_conn = transition_to(std::move(m_disc), Connecting{1});
    if (m_conn.data().attempt != 1) return false;
    m_conn.data_mut().attempt = 2;
    auto m_done = transition_to(std::move(m_conn), Connected{42});
    auto m_same = transition_to(std::move(m_done), Connected{43});
    return std::move(m_same).extract().fd == 43;
}
static_assert(walks_the_relation());

}  // namespace detail::machine_self_test

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
#define CRUCIBLE_ALLOW_MACHINE_TRANSITION(From, To)                                                               \
    namespace fixy::machine::admitted_transitions {                                                               \
    constexpr ::foundation::fail_closed::edge<From, To> CRUCIBLE_MACHINE_EDGE_CAT_(machine_edge_, __COUNTER__){}; \
    }
