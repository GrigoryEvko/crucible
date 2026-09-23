// Liveness by construction needs a projection onto each role.  Carol
// appears in one branch of a choice that she does not see.  In the other
// branch nobody sends to her, so she would wait for ever.
//
// The type is balanced+, so the refusal comes from the projection.

#include <fixy/session/Liveness.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Left {};
struct Right {};
struct Note {};

using OneBranch = g::Comm<Alice, Bob, g::Branch<Left, int, g::Msg<Bob, Carol, Note, int, g::End>>,
                          g::Branch<Right, int, g::End>>;

static_assert(g::is_balanced_plus_v<OneBranch>);

constexpr int require_live() noexcept {
    s::ensure_live_by_construction<OneBranch>();
    return 0;
}

}  // namespace

int main() { return require_live(); }
