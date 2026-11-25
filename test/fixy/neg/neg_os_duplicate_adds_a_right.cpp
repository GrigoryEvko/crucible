// A duplicate holds rights that its borrow holds.  A borrow of a socket with
// the read right alone gives no duplicate that writes.

#include <fixy/os/Handle.h>
#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace os = fixy::os;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream> const socket{};
    static_cast<void>(os::mint_duplicate<os::RightSet::write>(ctx, socket.ref().restrict<os::RightSet::read>()));
    return 0;
}
