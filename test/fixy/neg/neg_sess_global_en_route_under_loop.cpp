// Pischke, Masters and Yoshida, equation (51), G1.  An en-route message
// inside a loop that loops back adds one message to the queue for each
// iteration, so the queue has no fixed length.
//
// The type is well-formed and balanced: Alice and Bob act on each path
// of the loop.  So the refusal comes from the en-route count clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Alice {};
struct Bob {};
struct Queued {};
struct Tick {};

using Unbounded = g::Rec<g::EnRoute<Alice, Bob, Queued, int, g::Msg<Alice, Bob, Tick, int, g::Var>>>;

static_assert(g::is_balanced_v<Unbounded>);

constexpr int declare_protocol() noexcept {
    g::ensure_balanced_plus<Unbounded>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
