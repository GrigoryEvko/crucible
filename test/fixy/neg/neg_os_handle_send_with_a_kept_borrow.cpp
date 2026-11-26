// A thread that keeps a borrow of a socket gives the Handle to a second thread
// by move.  The second thread closes it, and the kernel can give its number to
// a later socket.  A door of the first thread with the borrow can then act on
// the later socket, between the check of the generation and its call.  A
// Handle stays on its thread (thread_bound{} of foundation/Transfer.h), so the
// thread door refuses the body.  The transit of fixy/os/Handle.h moves the
// Handle and makes the borrow stale first.

#include <fixy/Ctx.h>
#include <fixy/os/Socket.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using TcpSocket = net::SocketHandle<net::socket_kind::TcpStream>;
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
}  // namespace

int main() {
    TcpSocket socket{};
    auto const borrow = socket.ref();
    auto closer = [moved = std::move(socket)]() mutable noexcept { moved = TcpSocket{}; };
    static_cast<void>(fixy::thread::run_joined(fixy::BgLoadCtx{eff::testing::bg()}, fixy::thread::ThreadOptions{},
                                               std::move(closer)));
    return net::read_tcp_congestion(IoBlockCtx{eff::testing::test()}, borrow).is_ok() ? 0 : 1;
}
