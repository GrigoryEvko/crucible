// The value that a body gives back beside its two parts is no linear value.
// The frame gives the value in a Record beside the Handle of the parts, and
// a linear value there would end with the Record in silence.  The body here
// gives back the transit of another socket, which owes a consume, so the door
// refuses the body with its reason.

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
void split(IoBlockCtx const& ctx, Socket socket, Socket& other) noexcept {
    static_cast<void>(net::with_stream_halves(ctx, std::move(socket), [&other](auto read, auto write) noexcept {
        auto transit = std::move(other).depart();
        return fixy::Record<decltype(read), decltype(write), decltype(transit)>{std::move(read), std::move(write),
                                                                                std::move(transit)};
    }));
}
}  // namespace

int main() {
    UnixSocket other{};
    split(IoBlockCtx{eff::testing::test()}, UnixSocket{}, other);
    return 0;
}
