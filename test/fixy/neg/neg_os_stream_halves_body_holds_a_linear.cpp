// A body of the frame holds a linear value by value: the transit of another
// socket.  The door calls the body, and a call is no consume, so the transit
// would end with the body after the call, where no rule reads its end.  The
// deleted twin of the door refuses the body with the reason.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    UnixSocket other{};
    net::with_stream_halves(ctx, UnixSocket{}, [held = std::move(other).depart()](auto read, auto write) noexcept {
        return fixy::Record<decltype(read), decltype(write)>{std::move(read), std::move(write)};
    });
    return 0;
}
