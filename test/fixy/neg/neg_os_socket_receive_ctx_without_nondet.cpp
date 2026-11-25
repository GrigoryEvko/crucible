// A receive gives the bytes of a peer, and the time and the order of their
// arrival change from run to run.  The syscall catalog lifts recvmsg, and so
// the network family, to IO, Block and Nondet.  A context that holds IO and
// Block and no Nondet cannot receive.  The failure is the constraint of the
// socket call, at the call.

#include <fixy/os/Socket.h>
#include <foundation/core/Array.h>

namespace eff = foundation::effects;

namespace {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

[[maybe_unused]] void attempt(IoBlockCtx const& ctx,
                              fixy::net::SocketHandle<fixy::net::socket_kind::NetlinkRoute> const& socket) {
    auto [buffer, buffer_key] = foundation::core::mint_keyed_owner(foundation::core::fixed_array_owner<std::byte, 4>);
    buffer.with_mut(buffer_key, [&ctx, &socket](auto run) noexcept {
        [[maybe_unused]] auto received = fixy::net::receive_bytes(ctx, socket.ref(), std::move(run));
    });
}

}  // namespace

int main() { return 0; }
