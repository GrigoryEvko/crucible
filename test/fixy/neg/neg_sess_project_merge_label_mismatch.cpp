// Pischke, Masters and Yoshida, Example 11, G''.  Carol does not see the
// choice between Alice and Bob, and she must send a different label in
// each branch.  A sender cannot select on a choice it did not see.
//
// The type is well-formed and balanced, so only the projection refuses.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Dave {};
struct Left {};
struct Right {};
struct Hello {};
struct Goodbye {};

using Blind = g::Comm<Alice, Bob, g::Branch<Left, int, g::Msg<Carol, Dave, Hello, int, g::End>>,
                      g::Branch<Right, int, g::Msg<Carol, Dave, Goodbye, int, g::End>>>;

static_assert(g::is_balanced_plus_v<Blind>);

constexpr int project_carol() noexcept {
    s::ensure_projectable<Blind, Carol>();
    return 0;
}

}  // namespace

int main() { return project_carol(); }
