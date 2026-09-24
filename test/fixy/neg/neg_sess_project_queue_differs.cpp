// Pischke, Masters and Yoshida, equation (51), G4.  Alice has a message
// en route to Bob in one branch of a choice she does not see, and none in
// the other.  Her queue would depend on a choice she cannot know, so the
// projection onto her refuses.
//
// The type is well-formed, so the refusal comes from the queue rule of
// the projection.

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
struct Note {};

using OneBranchQueued = g::Comm<Carol, Dave, g::Branch<Left, int, g::Msg<Alice, Bob, Note, int, g::End>>,
                                g::Branch<Right, int, g::EnRoute<Alice, Bob, Note, int, g::End>>>;

static_assert(g::is_global_well_formed_v<OneBranchQueued>);

constexpr int project_alice() noexcept {
    s::ensure_projectable<OneBranchQueued, Alice>();
    return 0;
}

}  // namespace

int main() { return project_alice(); }
