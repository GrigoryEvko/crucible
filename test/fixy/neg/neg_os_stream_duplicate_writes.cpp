// A duplicate of a stream socket shares its stream, and write_full makes more
// than one call for one run.  A write of the other handle between two calls
// puts its bytes into the middle of the run.  So a duplicate of a TCP stream
// with the write right does not compile: two handles would write one stream.

#include <fixy/os/Handle.h>
#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace os = fixy::os;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    net::SocketHandle<net::socket_kind::TcpStream> const socket{};
    static_cast<void>(os::mint_duplicate<os::RightSet::write>(ctx, socket.ref()));
    return 0;
}
