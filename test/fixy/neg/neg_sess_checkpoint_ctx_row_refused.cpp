// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A checkpoint session sends a payload that carries IO, and the context
// of the mint holds no IO.  The context of a checkpoint session admits
// the effect row of each payload, as the context of mint_session does, so
// the mint refuses the context.
//
// Expected diagnostic: the context does not admit the row of the protocol.
#include <fixy/session/Checkpoint.h>

#include <foundation/effects/Computation.h>

namespace neg_sess_checkpoint_ctx_row_refused_types {
struct Wire {};
}  // namespace neg_sess_checkpoint_ctx_row_refused_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_checkpoint_ctx_row_refused_types;
    using Io = eff::Computation<eff::Row<eff::Effect::IO>, int>;
    using Decide = s::Select<s::Commit<s::Send<Io, s::End>>, s::Roll>;
    using Follow = s::Offer<s::Commit<s::Recv<Io, s::End>>, s::Roll>;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto handle = s::mint_checkpoint_session<Decide, Follow>(ctx, Wire{});
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
