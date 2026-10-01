// The compile-time checks of fixy/Machine.h.

#include <fixy/Machine.h>

namespace fixy {

static_assert(sizeof(Machine<int>) == sizeof(int));
static_assert(sizeof(Machine<void*>) == sizeof(void*));

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
