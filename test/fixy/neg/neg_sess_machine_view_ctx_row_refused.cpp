// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A machine bridge runs a protocol that sends a payload that carries IO,
// and the context of the view holds no IO.  The gate of session_view is
// the gate of mint_session over the reference to the bridge, so the view
// refuses the context.
//
// Expected diagnostic: the context does not admit the row of the protocol.
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Computation.h>

namespace neg_sess_machine_view_ctx_row_refused_types {
struct Payload {
    int ticks = 0;
};
}  // namespace neg_sess_machine_view_ctx_row_refused_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_machine_view_ctx_row_refused_types;
    using SendsIo = s::Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, s::End>;
    auto bridge = s::mint_session_from_machine<SendsIo, Payload>();
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto view = bridge.session_view(ctx);
    std::move(view).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
