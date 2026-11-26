// A duplicate of a stream socket shares its stream, and read_full makes more
// than one call for one run.  A read of the other handle between two calls
// takes bytes from the middle of the run.  So a duplicate of a TCP stream with
// the read right does not compile: two handles would read one stream.

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
    static_cast<void>(os::mint_duplicate<os::RightSet::read>(ctx, socket.ref()));
    return 0;
}
