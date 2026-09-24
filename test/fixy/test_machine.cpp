// Sentinel TU for fixy/Machine.h: the machine costs its state, the
// constructor is behind its two doors, a transition is admitted only by
// an edge in the relation the machine names, the three Mach.h helpers
// project and gate on that relation, and the walk runs a second time
// through a volatile seed so the state is not folded away.
//
// Ported from test/test_fixy_mach_transitions.cpp and the Machine cells
// of test/test_fixy_mach_safety.cpp.  The two edges the old test opted
// in through CRUCIBLE_ALLOW_MACHINE_TRANSITION are declared once each
// way: in a per-machine namespace, and in the shared relation through
// the macro, so both spellings are exercised.

#include <fixy/Machine.h>

#include <foundation/diag/FailClosed.h>

#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fmach = ::fixy::mach;
using ::fixy::Machine;
using ::fixy::MachineTransition;
using ::fixy::mint_machine;
using ::fixy::transition_to;

struct Disconnected {};

struct Connecting {
    int attempt = 0;
    explicit Connecting(int a) noexcept : attempt{a} {}
};

struct Connected {
    int fd = -1;
    explicit Connected(int f) noexcept : fd{f} {}
};

// The per-machine relation, preferred: the edges sit beside the states
// they join, and no other machine reads them.
namespace connection_edges {
inline constexpr ::foundation::fail_closed::edge<Disconnected, Connecting> disconnected_to_connecting{};
inline constexpr ::foundation::fail_closed::edge<Connecting, Connected> connecting_to_connected{};
}  // namespace connection_edges

using ConnMachine = Machine<Disconnected, ^^connection_edges>;

// The shared relation, through the macro: the same two edges, for a
// machine that names no relation of its own.
CRUCIBLE_ALLOW_MACHINE_TRANSITION(Disconnected, Connecting)
CRUCIBLE_ALLOW_MACHINE_TRANSITION(Connecting, Connected)

// The relation is what the machine names, and nothing else.
static_assert(MachineTransition<Disconnected, Connecting, ^^connection_edges>);
static_assert(MachineTransition<Connecting, Connected, ^^connection_edges>);
static_assert(!MachineTransition<Connected, Connecting, ^^connection_edges>, "the inverse needs its own edge");
static_assert(!MachineTransition<Disconnected, Connected, ^^connection_edges>, "no edge skips a state");
static_assert(MachineTransition<Disconnected, Connecting>, "the macro appended to the shared relation");
static_assert(MachineTransition<Connecting, Connected>);
static_assert(!MachineTransition<Connected, Disconnected>);

// The three helpers of the old fixy/Mach.h.
static_assert(std::is_same_v<fmach::state_of_t<ConnMachine>, Disconnected>,
              "state_of_t must project Machine<Disconnected>'s state_type.");

static_assert(std::is_same_v<fmach::state_of_t<Machine<Connecting, ^^connection_edges>&>, Connecting>,
              "state_of_t must strip ref qualifiers.");

static_assert(std::is_same_v<fmach::state_of_t<Machine<Connected, ^^connection_edges>&&>, Connected>,
              "state_of_t must strip rvalue-ref qualifiers.");

static_assert(fmach::is_machine_v<Machine<int>>, "is_machine_v must accept Machine<int>.");
static_assert(fmach::is_machine_v<Machine<Connecting, ^^connection_edges>&>,
              "is_machine_v must strip ref before testing.");
static_assert(!fmach::is_machine_v<int>, "is_machine_v must reject bare int.");
static_assert(!fmach::is_machine_v<Disconnected>, "is_machine_v must reject bare State type (not yet wrapped).");

static_assert(fmach::can_transition_v<ConnMachine, Connecting>,
              "Disconnected -> Connecting must be a valid transition.");

static_assert(fmach::can_transition_v<Machine<Connecting, ^^connection_edges>, Connected>,
              "Connecting -> Connected must be a valid transition.");

static_assert(fmach::can_transition_v<Machine<Disconnected>, Connecting>,
              "the shared relation admits the macro's edge for a machine that names no relation.");

static_assert(!fmach::can_transition_v<int, Connecting>, "can_transition_v must reject when M is not a Machine.");

static_assert(!fmach::can_transition_v<Disconnected, Connecting>,
              "can_transition_v must reject when M is a bare State (must be "
              "wrapped in Machine<>).");

// The machine costs its state and has no public constructor.
static_assert(sizeof(ConnMachine) == sizeof(Disconnected));
static_assert(sizeof(Machine<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(!std::is_copy_constructible_v<ConnMachine>);
static_assert(std::is_nothrow_move_constructible_v<ConnMachine>);
static_assert(!std::is_constructible_v<ConnMachine, Disconnected>);
static_assert(!std::is_constructible_v<ConnMachine, std::in_place_t>);
static_assert(!std::is_default_constructible_v<ConnMachine>);

// A machine over one relation is a different type from the same states
// over another, so an edge admitted in one cannot be borrowed by the other.
static_assert(!std::is_same_v<ConnMachine, Machine<Disconnected>>);
static_assert(ConnMachine::edges == ^^connection_edges);
static_assert(Machine<Disconnected>::edges == ^^fixy::machine::admitted_transitions);

// Both relations walk the same path in a constant expression.
[[nodiscard]] consteval int walk_per_machine() noexcept {
    auto m_disc = mint_machine<Disconnected, ^^connection_edges>();
    auto m_conn = transition_to(std::move(m_disc), Connecting{1});
    auto m_done = transition_to(std::move(m_conn), Connected{42});
    return m_done.data().fd;
}
static_assert(walk_per_machine() == 42);

[[nodiscard]] consteval int walk_shared() noexcept {
    auto m_disc = mint_machine<Disconnected>();
    auto m_conn = transition_to(std::move(m_disc), Connecting{1});
    auto m_done = transition_to(std::move(m_conn), Connected{43});
    return std::move(m_done).extract().fd;
}
static_assert(walk_shared() == 43);

namespace {

// The same two walks a second time, through a volatile seed, so the
// state travels in a register rather than being folded away.
int check_runtime_walk() {
    volatile int seed = 42;
    auto m = mint_machine<int>(static_cast<int>(seed));
    if (m.data() != 42) return 10;
    auto m2 = transition_to(std::move(m), static_cast<int>(seed) + 57);
    if (m2.data() != 99) return 11;

    auto m_disc = mint_machine<Disconnected, ^^connection_edges>();
    auto m_conn = transition_to(std::move(m_disc), Connecting{static_cast<int>(seed)});
    auto m_done = transition_to(std::move(m_conn), Connected{static_cast<int>(seed) + 1});
    if (m_done.data().fd != 43) return 12;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_walk(); rc != 0) return rc;

    auto m = mint_machine<int>(42);
    auto m2 = transition_to(std::move(m), int{99});
    if (m2.data() != 99) return 1;

    auto m_disc = mint_machine<Disconnected, ^^connection_edges>();
    auto m_conn = transition_to(std::move(m_disc), Connecting{1});
    m_conn.data_mut().attempt = 2;
    if (m_conn.data().attempt != 2) return 2;
    auto m_done = transition_to(std::move(m_conn), Connected{42});
    return m_done.data().fd == 42 ? 0 : 3;
}
