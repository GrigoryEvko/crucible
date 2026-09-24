// Tirore, Bengtson and Carbone, ECOOP 2025, equation (1).  Carol
// receives from Bob in one branch and from Alice in the other.  With one
// queue for each ordered pair of roles Carol must know which queue to
// read, and she does not see the choice.  This is the shape that refutes
// the subject reduction of Honda, Yoshida and Carbone when roles share a
// queue.
//
// The type is well-formed and balanced+, so only the projection refuses.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct First {};
struct Second {};
struct Flag {};

using SharedQueueShape = g::Comm<Alice, Bob, g::Branch<First, int, g::Msg<Bob, Carol, Flag, bool, g::End>>,
                                 g::Branch<Second, int, g::Msg<Alice, Carol, Flag, bool, g::End>>>;

static_assert(g::is_balanced_plus_v<SharedQueueShape>);

constexpr int project_carol() noexcept {
    s::ensure_projectable<SharedQueueShape, Carol>();
    return 0;
}

}  // namespace

int main() { return project_carol(); }
