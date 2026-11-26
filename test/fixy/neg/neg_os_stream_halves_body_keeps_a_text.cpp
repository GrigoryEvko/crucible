// A body of the frame keeps a text in a variable that it captures by
// reference.  The text is a borrow, and the caller reads the variable after
// the frame, so a text of memory that the frame ends would dangle there.  The
// store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the body, whatever the source of
// the text.

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
    fixy::TextView kept{};
    net::with_stream_halves(ctx, UnixSocket{}, [&kept](auto read, auto write) noexcept {
        kept = fixy::TextView{"kept"};
        return fixy::Record<decltype(read), decltype(write)>{std::move(read), std::move(write)};
    });
    return kept.size() == 0 ? 0 : 1;
}
