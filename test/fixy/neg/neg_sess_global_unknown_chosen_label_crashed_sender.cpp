// A crashed sender keeps the message it sent before the crash, so its
// en-route node keeps a chosen label that is not the crash label.  That
// label must still name a branch.
//
// The node is under a message prefix, so the walk reaches it through the
// continuation.  The labels are distinct and the crash branch carries no
// payload, so the only clause that can refuse is the chosen-label clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};
struct Start {};

using Pending = g::EnRouteChoice<g::Crashed<Alice>, Bob, Pong, g::Branch<Ping, int, g::End>,
                                 g::Branch<g::CrashLabel, void, g::End>>;
using Prefixed = g::Msg<Carol, Bob, Start, void, Pending>;

constexpr int declare_protocol() noexcept {
    g::ensure_global_well_formed<Prefixed>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
