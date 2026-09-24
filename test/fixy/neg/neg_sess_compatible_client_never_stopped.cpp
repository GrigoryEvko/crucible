// The client waits for a stop that the server never sends.  The client
// refines the dual of the server, because a receiver keeps no exit.  The
// server does not refine the dual of the client, because it drops the
// stop.  Compatibility asks for both directions and refuses the pair.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Job {};
struct StopCmd {};
using Server = s::Loop<s::Select<s::Send<Job, s::Continue>>>;
using Client = s::Loop<s::Offer<s::Recv<Job, s::Continue>, s::Recv<StopCmd, s::End>>>;

}  // namespace

int main() {
    s::assert_compatible_client<Client, Server>();
    return 0;
}
