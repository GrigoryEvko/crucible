// mint_socket_pair needs a context that admits Block as well as IO.  The gate
// reads the row off the socketpair syscall atom, and the catalog puts
// socketpair in the network family, which lifts to IO and Block.  A context
// that holds IO alone is refused at the requires-clause.

#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
// Admits IO but not Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    [[maybe_unused]] auto pair = net::mint_socket_pair<net::socket_kind::UnixStream>(ctx);
    return 0;
}
