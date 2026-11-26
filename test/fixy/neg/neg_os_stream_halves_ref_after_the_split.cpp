// A borrow of a stream socket is read after with_stream_halves, which splits
// the Handle into two halves and advances the generation of its number.  The
// parameter of the frame names the Handle by an rvalue reference, so the call
// ends each borrow of the number, and the rule view-escape refuses the read.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Socket.h>

#include <utility>

namespace eff = ::foundation::effects;
namespace net = ::fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using UnixSocket = net::SocketHandle<net::socket_kind::UnixStream>;
using UnixKind = ::fixy::os::kind::Socket<net::socket_kind::UnixStream>;
using Refused = net::StreamHalvesRefused<UnixKind, net::socket_rights>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    UnixSocket socket{};
    auto const borrow = socket.ref();
    bool const is_joined =
        net::with_stream_halves(ctx, std::move(socket), [](auto read, auto write) noexcept {
            return ::fixy::Record<decltype(read), decltype(write)>{std::move(read), std::move(write)};
        }).match([](auto joined) noexcept { return joined.is_open(); }, [](Refused) noexcept { return false; });
    return is_joined && borrow.is_open() ? 1 : 0;
}
