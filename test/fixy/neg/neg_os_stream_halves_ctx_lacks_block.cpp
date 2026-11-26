// with_stream_halves needs a context that admits Block as well as IO.  The
// split gives a second number to the open file of the socket, and the call can
// wait while the table of the numbers of the process grows.  A context that
// holds IO alone is refused at the requires-clause.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
// Admits IO but not Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
}  // namespace

int main() {
    IoOnlyCtx const ctx{eff::testing::init()};
    return net::with_stream_halves(ctx, UnixSocket{},
                                   [](auto read, auto write) noexcept {
                                       return fixy::Record<decltype(read), decltype(write)>{std::move(read),
                                                                                            std::move(write)};
                                   })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
