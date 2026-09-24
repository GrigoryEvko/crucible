// The chosen label of an en-route node is the branch that the sender
// sent.  A chosen label that names no branch leaves the receiver no
// continuation to take.
//
// The roles are distinct, no role is crashed and the labels are
// distinct, so the only clause that can refuse is the chosen-label
// clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Client {};
struct Server {};
struct Ask {};
struct Tell {};
struct Quit {};

using Sent = g::EnRouteChoice<Client, Server, Quit, g::Branch<Ask, int, g::End>, g::Branch<Tell, char, g::End>>;

constexpr int declare_protocol() noexcept {
    g::ensure_global_well_formed<Sent>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
