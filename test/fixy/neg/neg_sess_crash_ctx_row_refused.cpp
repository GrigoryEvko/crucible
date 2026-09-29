// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A crash session receives a payload that carries IO, and the context of
// the mint holds no IO.  The context of a crash session admits the effect
// row of each payload, as the context of mint_session does, so the mint
// refuses the context.
//
// Expected diagnostic: the context does not admit the row of the protocol.
#include <fixy/session/CrashTransport.h>

#include <foundation/effects/Computation.h>

namespace neg_sess_crash_ctx_row_refused_types {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace neg_sess_crash_ctx_row_refused_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_crash_ctx_row_refused_types;
    using ReceivesIo =
        s::Offer<s::Recv<eff::Computation<eff::Row<eff::Effect::IO>, int>, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    auto handle = s::mint_crash_session<ReceivesIo, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
