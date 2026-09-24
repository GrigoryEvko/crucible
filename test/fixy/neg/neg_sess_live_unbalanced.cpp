// Liveness by construction needs a balanced+ global type (Pischke,
// Masters, Yoshida, Theorem 13).  In this loop Alice can choose Again
// for ever, and Carol, a role of the loop body, then waits for ever.

#include <fixy/session/Liveness.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Again {};
struct Stop {};
struct Note {};

using Starves = g::Rec<g::Comm<Alice, Bob, g::Branch<Again, int, g::Var>,
                               g::Branch<Stop, int, g::Msg<Alice, Carol, Note, int, g::End>>>>;

constexpr int require_live() noexcept {
    s::ensure_live_by_construction<Starves>();
    return 0;
}

}  // namespace

int main() { return require_live(); }
