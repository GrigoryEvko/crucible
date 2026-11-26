// The brand of a frame is the scope brand of its door, which no caller names.
// A caller that spells a brand could give two frames one brand, and the join
// of one frame could then take a half of the other.  The first template
// parameter of the door is a pack that takes each explicit argument, and the
// gate refuses a pack that is not empty.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
struct SpelledBrand {};
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    return net::with_stream_halves<SpelledBrand>(ctx, UnixSocket{},
                                                 [](auto read, auto write) noexcept {
                                                     return fixy::Record<decltype(read), decltype(write)>{
                                                         std::move(read), std::move(write)};
                                                 })
        .match([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
}
