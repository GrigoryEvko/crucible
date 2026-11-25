// The growth of the table of the numbers of a process with threads waits for
// each reader of the old table, so a duplicate needs a context with Block.

#include <fixy/os/Handle.h>
#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace os = fixy::os;

namespace {
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx const ctx{eff::testing::test()};
    fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream> const socket{};
    static_cast<void>(os::mint_duplicate<fixy::net::socket_rights>(ctx, socket.ref()));
    return 0;
}
