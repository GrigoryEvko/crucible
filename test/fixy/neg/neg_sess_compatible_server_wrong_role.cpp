// The client expects Bob to pick the choice, and the server that picks it
// names itself Alice.  Duality keeps the Sender note, so the dual of the
// client is a Select that names Bob, and the server does not refine it.
// The server side of the check refuses the pair.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Order {};

using Client = s::Offer<s::Sender<Bob>, s::Recv<Order, s::End>>;
using Server = s::Select<s::Sender<Alice>, s::Send<Order, s::End>>;

}  // namespace

int main() {
    s::assert_compatible_server<Server, Client>();
    return 0;
}
