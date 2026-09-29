// The session mint of a lazily established channel reads the gate of
// fixy::session::mint_session.  The protocol sends a computation that
// does IO, and the background context holds no IO, so the mint is
// refused.  A mint that took no context would give the handle to any
// caller.

#include <fixy/handle/LazyEstablishedChannel.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace h = ::fixy::handle;
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace {
struct Wire : ::foundation::Pinned<Wire> {
    int sentinel = 0;
};

using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using SendsIo = s::Send<IoWork, s::End>;
}  // namespace

int main() {
    h::LazyEstablishedChannel<SendsIo, Wire> channel;
    Wire wire{};
    channel.establish(wire);

    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = channel.mint_established_session(ctx);
    return head.has_value() ? 0 : 1;
}
