// The client expects Bob to pick the choice, and the server that picks it
// names itself Alice.  Duality keeps the Sender note, so the dual of the
// server is an Offer that names Alice, and the client does not refine it.
// The client side of the check refuses the pair, as the server side does.

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
    s::assert_compatible_client<Client, Server>();
    return 0;
}
