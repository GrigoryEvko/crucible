// The value that a body gives back beside its two parts leaves the frame.  A
// value that names the brand of the frame, such as a token of the end of a
// half, could go into a later frame of the same brand and stand for a half
// that no door ended there.  The door refuses the body with its reason.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;

// The door gives no value for a body that it refuses, so the split stands in
// a function template, where the cast of that result to void is no warning.
template <class Socket>
void split(IoBlockCtx const& ctx, Socket socket) noexcept {
    static_cast<void>(net::with_stream_halves(ctx, std::move(socket), [](auto read, auto write) noexcept {
        auto read_end = std::move(read).close();
        auto write_end = std::move(write).shut_down_and_close();
        return fixy::Record<decltype(read_end), decltype(write_end), decltype(read_end)>{read_end, write_end, read_end};
    }));
}
}  // namespace

int main() {
    split(IoBlockCtx{eff::testing::test()}, UnixSocket{});
    return 0;
}
