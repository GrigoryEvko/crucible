// The supertype handles the crash of its peer, and the subtype does not.
// A crash branch has no position on the wire, so it is matched by the
// payload it receives, and the subtype has no branch with that payload
// (rule Sub-&).  The refusal names the class of the failure.

#include <fixy/session/Crash.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Peer {};
struct Ping {};
using NoRecovery = s::Offer<s::Recv<Ping, s::End>>;
using Recovers = s::Offer<s::Recv<Ping, s::End>, s::Recv<s::Crash<Peer>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<NoRecovery, Recovers>();
    return 0;
}
