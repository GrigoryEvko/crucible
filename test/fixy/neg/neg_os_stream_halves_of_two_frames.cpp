// The body of a frame gives back a half of another frame.  The inner frame
// divides a second socket, and its body gives back the write half of the
// outer frame in place of its own.  The join of the inner frame would close
// the number of the outer socket and give the Handle of the second socket
// its rights back while the outer frame still holds its other half.  The
// brand of each frame is its own scope brand, so the gate of the inner frame
// refuses the body.

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
    return net::with_stream_halves(ctx, UnixSocket{},
                                   [&ctx, &other](auto read, auto write) noexcept {
                                       static_cast<void>(net::with_stream_halves(
                                           ctx, std::move(other), [&write](auto inner_read, auto inner_write) noexcept {
                                               auto inner_end = std::move(inner_write).shut_down_and_close();
                                               static_cast<void>(inner_end);
                                               return fixy::Record<decltype(inner_read), decltype(write)>{
                                                   std::move(inner_read), std::move(write)};
                                           }));
                                       return fixy::Record<decltype(read), decltype(write)>{std::move(read),
                                                                                            std::move(write)};
                                   })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
