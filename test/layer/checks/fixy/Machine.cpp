// The compile-time checks of fixy/Machine.h.

#include <fixy/Machine.h>

#include <type_traits>
#include <utility>

namespace fixy {

// The machine costs its state and one flag, which takes the alignment of
// the state.
static_assert(sizeof(Machine<int>) == sizeof(int) + alignof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*) + alignof(void*));

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
// Every read counts the members against this seal, so a member that
// another file adds stops the build rather than widening the relation.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 2};
}  // namespace connection_edges

using ConnMachine = Machine<Disconnected, ^^connection_edges>;

static_assert(MachineTransition<Disconnected, Connecting, ^^connection_edges>);
static_assert(MachineTransition<Connecting, Connected, ^^connection_edges>);
static_assert(MachineTransition<Connected, Connected, ^^connection_edges>, "the diagonal needs no declaration");
static_assert(!MachineTransition<Connecting, Disconnected, ^^connection_edges>, "the relation is one-way");
static_assert(!MachineTransition<Disconnected, Connected, ^^connection_edges>, "no edge skips a state");
static_assert(!MachineTransition<Disconnected, Connecting>, "a machine that names no relation admits no edge");

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

// A move and a transition clear the flag of their source, and a move
// assignment gives the flag of its source to the target.  The source is a
// member that a callee empties, as a holder of a machine sees it.
struct HeldMachine {
    Machine<int> machine = mint_machine<int>(1);
};

[[nodiscard]] consteval Machine<int> take_machine(Machine<int>& held) noexcept { return std::move(held); }

[[nodiscard]] consteval Machine<int> step_machine(Machine<int>& held) noexcept {
    return transition_to(std::move(held), 2);
}

[[nodiscard]] consteval bool move_clears_the_source() noexcept {
    HeldMachine held{};
    Machine<int> taken = take_machine(held.machine);
    return !held.machine.is_live() && taken.is_live() && taken.data() == 1;
}
static_assert(move_clears_the_source());

[[nodiscard]] consteval bool transition_clears_the_source() noexcept {
    HeldMachine held{};
    Machine<int> next = step_machine(held.machine);
    return !held.machine.is_live() && next.is_live() && next.data() == 2;
}
static_assert(transition_clears_the_source());

[[nodiscard]] consteval bool assignment_builds_a_spent_machine_again() noexcept {
    HeldMachine held{};
    Machine<int> taken = take_machine(held.machine);
    held.machine = take_machine(taken);
    return held.machine.is_live() && !taken.is_live() && held.machine.data() == 1;
}
static_assert(assignment_builds_a_spent_machine_again());

static_assert(!std::is_trivially_copyable_v<Machine<int>>);
static_assert(std::is_nothrow_move_constructible_v<Machine<int>>);
static_assert(std::is_nothrow_move_assignable_v<Machine<int>>);

// Each data member of the machine has an initializer, also over a state
// with no default constructor.  The members of the class, the private ones
// too, come as a constant array, so the loop reads no container.
template <std::meta::info Type>
inline constexpr std::meta::info data_members_of_ = std::meta::reflect_constant_array(
    std::meta::nonstatic_data_members_of(Type, std::meta::access_context::unchecked()));

template <std::meta::info Type>
[[nodiscard]] consteval bool each_member_has_initializer() {
    for (std::meta::info const member : [:data_members_of_<Type>:]) {
        if (!std::meta::has_default_member_initializer(member)) return false;
    }
    return true;
}
static_assert(each_member_has_initializer<^^Machine<int>>());
static_assert(each_member_has_initializer<^^Machine<Connecting, ^^connection_edges>>());
static_assert(!std::is_default_constructible_v<Connecting>);

}  // namespace detail::machine_self_test

}  // namespace fixy
