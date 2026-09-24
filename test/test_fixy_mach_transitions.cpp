// Sentinel TU: compiles the alias header under the project warning flags so its
// static_asserts run.

#include <crucible/fixy/_Mach.h>

#include <type_traits>
#include <utility>

namespace fmach = crucible::fixy::mach;
namespace saf = crucible::safety;

struct Disconnected {};

struct Connecting {
    int attempt = 0;
    explicit Connecting(int a) noexcept : attempt{a} {}
};

struct Connected {
    int fd = -1;
    explicit Connected(int f) noexcept : fd{f} {}
};

CRUCIBLE_ALLOW_MACHINE_TRANSITION(Disconnected, Connecting)
CRUCIBLE_ALLOW_MACHINE_TRANSITION(Connecting, Connected)

static_assert(std::is_same_v<fmach::state_of_t<saf::Machine<Disconnected>>, Disconnected>,
              "state_of_t must project Machine<Disconnected>'s state_type.");

static_assert(std::is_same_v<fmach::state_of_t<saf::Machine<Connecting>&>, Connecting>,
              "state_of_t must strip ref qualifiers.");

static_assert(std::is_same_v<fmach::state_of_t<saf::Machine<Connected>&&>, Connected>,
              "state_of_t must strip rvalue-ref qualifiers.");

static_assert(fmach::is_machine_v<saf::Machine<int>>, "is_machine_v must accept Machine<int>.");
static_assert(fmach::is_machine_v<saf::Machine<Connecting>&>, "is_machine_v must strip ref before testing.");
static_assert(!fmach::is_machine_v<int>, "is_machine_v must reject bare int.");
static_assert(!fmach::is_machine_v<Disconnected>, "is_machine_v must reject bare State type (not yet wrapped).");

static_assert(fmach::can_transition_v<saf::Machine<Disconnected>, Connecting>,
              "Disconnected -> Connecting must be a valid transition.");

static_assert(fmach::can_transition_v<saf::Machine<Connecting>, Connected>,
              "Connecting -> Connected must be a valid transition.");

static_assert(!fmach::can_transition_v<int, Connecting>, "can_transition_v must reject when M is not a Machine.");

static_assert(!fmach::can_transition_v<Disconnected, Connecting>,
              "can_transition_v must reject when M is a bare State (must be "
              "wrapped in Machine<>).");

int main() {
    auto m_disc = fmach::mint_machine<Disconnected>();
    auto m_conn = fmach::transition_to(std::move(m_disc), Connecting{1});
    auto m_done = fmach::transition_to(std::move(m_conn), Connected{42});
    return m_done.data().fd == 42 ? 0 : 1;
}
