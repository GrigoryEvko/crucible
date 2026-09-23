// Pischke, Masters and Yoshida, equation (46), G1.  The loop can take
// the first branch for ever.  Carol is a role of the loop body, and no
// path of that branch reaches her, so she has no bounded depth and can
// wait for ever.
//
// The type is well-formed: distinct roles, distinct labels, a guarded
// loop.  So the refusal comes from the balancedness clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Alice {};
struct Bob {};
struct Carol {};
struct Again {};
struct Stop {};
struct Note {};

using Starves = g::Rec<g::Comm<Alice, Bob, g::Branch<Again, int, g::Var>,
                               g::Branch<Stop, int, g::Msg<Alice, Carol, Note, int, g::End>>>>;

static_assert(g::is_global_well_formed_v<Starves>);

constexpr int declare_protocol() noexcept {
    g::ensure_balanced_plus<Starves>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
