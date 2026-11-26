// A borrow of the write half of a stream socket is read after
// shut_down_and_close(), which closes the number of the half.  The half owns
// its number (owns_by_number{}), and the door takes it by an rvalue reference,
// so the door ends each borrow of the number, and the rule view-escape refuses
// the read.

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
    bool is_open_after_the_end = false;
    bool const is_joined =
        net::with_stream_halves(ctx, UnixSocket{}, [&is_open_after_the_end](auto read, auto write) noexcept {
            auto const borrow = write.ref();
            auto ended = std::move(write).shut_down_and_close();
            is_open_after_the_end = borrow.is_open();
            return ::fixy::Record<decltype(read), decltype(ended)>{std::move(read), std::move(ended)};
        }).match([](auto joined) noexcept { return joined.is_open(); }, [](Refused) noexcept { return false; });
    return is_joined && is_open_after_the_end ? 1 : 0;
}
