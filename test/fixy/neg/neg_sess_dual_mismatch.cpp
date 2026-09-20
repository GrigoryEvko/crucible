// Two endpoints of one channel must be structural duals, or the
// deadlock-freedom guarantee does not hold: one side waits to receive
// a message the other side never agreed to send.
//
// ensure_dual is the CURRENT entry point for that check, and this
// fixture calls it with a genuinely non-dual pair.  Both protocols
// below are individually well-formed, so nothing else in the header
// has grounds to refuse them — the rejection can only come from the
// duality clause itself.
//
// The mismatch is a Send facing a Send.  One endpoint offers to write
// Msg and so does the other, so neither is ever positioned to read.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Msg {};

using Mine = s::Send<Msg, s::End>;
// The dual of Mine is Recv<Msg, End>.  This is Send again.
using Theirs = s::Send<Msg, s::End>;

static_assert(s::is_well_formed_v<Mine>);
static_assert(s::is_well_formed_v<Theirs>);

constexpr int pair_them() noexcept {
    s::ensure_dual<Mine, Theirs>();
    return 0;
}

}  // namespace

int main() { return pair_them(); }
