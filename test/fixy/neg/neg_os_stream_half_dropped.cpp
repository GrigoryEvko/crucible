// A half of a stream socket ends at a named door: close() of the read half,
// shut_down_and_close() of the write half, or the end of its frame.  It owes
// a consume at its end, so the generic drop door of foundation/Linear.h
// refuses it.  A drop would end the half in silence, and the body would give
// back no part of the frame for it.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>
#include <foundation/Linear.h>

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
                                   [](auto read, auto write) noexcept -> fixy::Record<decltype(read), decltype(write)> {
                                       ::foundation::linear::drop(std::move(write));
                                       return {std::move(read), std::move(write)};
                                   })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
