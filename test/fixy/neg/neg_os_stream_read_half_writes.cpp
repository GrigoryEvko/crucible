// The borrow of a read half holds no write right, so the write door refuses
// it.  A write through the read half would put bytes into the stream beside
// the write half of another thread, and the peer would read the bytes of the
// two writers mixed.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
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
    return net::with_stream_halves(ctx, UnixSocket{},
                                   [&ctx](auto read, auto write) noexcept {
                                       static_cast<void>(fixy::fs::write_full(ctx, read.ref(), fixy::TextView{"x"}));
                                       return fixy::Record<decltype(read), decltype(write)>{std::move(read),
                                                                                            std::move(write)};
                                   })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
