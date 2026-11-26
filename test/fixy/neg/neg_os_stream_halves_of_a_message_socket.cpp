// with_stream_halves divides only a stream socket.  A socket that keeps one
// message for each call, such as a netlink socket, has no stream that a read
// half and a write half divide: a message of one direction and a message of
// the other are one unit of the kernel.  Its tag states the message shape, so
// the gate of the frame refuses the split.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using NetlinkSocket = net::SocketHandle<net::socket_kind::NetlinkRoute>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    return net::with_stream_halves(ctx, NetlinkSocket{},
                                   [](auto read, auto write) noexcept {
                                       return fixy::Record<decltype(read), decltype(write)>{std::move(read),
                                                                                            std::move(write)};
                                   })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
