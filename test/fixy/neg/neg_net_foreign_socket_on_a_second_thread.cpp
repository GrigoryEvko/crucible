// A door of a second thread can act on the number of a ref while the thread of
// the Handle closes it, so a ref of a socket stays on its thread, and a body of
// a second thread does not carry one.  The second thread holds a duplicate of
// its own (fixy/os/Handle.h).

#include <fixy/Ctx.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using SharedTcp = net::ForeignSocketRef<net::socket_kind::TcpStream, net::socket_rights>;
}  // namespace

int main() {
    net::SocketHandle<net::socket_kind::TcpStream> const socket{};
    SharedTcp const shared = net::share_socket(socket.ref());
    auto reader = [shared]() noexcept { static_cast<void>(shared.is_current()); };
    static_cast<void>(fixy::thread::run_joined(fixy::BgLoadCtx{eff::testing::bg()}, fixy::thread::ThreadOptions{},
                                               std::move(reader)));
    return 0;
}
