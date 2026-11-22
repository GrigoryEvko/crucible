// Negative compile test.  This file must fail to compile.
//
// A machine bridge runs a protocol that sends a payload that carries IO,
// and the context of the view holds no IO.  The gate of with_view is the
// gate of with_session over the lease of the machine, so the view refuses
// the context.
//
// Expected diagnostic: the context does not admit the row of the protocol.
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Computation.h>

#include <utility>

namespace neg_sess_machine_view_ctx_row_refused_types {
struct Payload {
    int ticks = 0;
};
}  // namespace neg_sess_machine_view_ctx_row_refused_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_machine_view_ctx_row_refused_types;
    using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
    using SendsIo = s::Send<IoWork, s::End>;
    auto bridge = s::mint_session_from_machine<SendsIo, Payload>();
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    bridge.with_view(ctx, [](auto head) noexcept {
        return std::move(head).send(IoWork{}, [](auto&, IoWork&) noexcept { return true; });
    });
    return 0;
}
