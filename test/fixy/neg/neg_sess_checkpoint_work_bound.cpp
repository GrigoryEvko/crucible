// Negative compile test.  This file must fail to compile.
//
// The compliance check of a checkpoint pair has a bound of work, the
// template argument WorkBound of the mint.  The pair below is compliant, and
// its check needs more than the 64 units of work that the mint gives it.
// The refusal names the bound and the constant of the default bound, so the
// caller knows what to raise.
//
// Expected diagnostic: Checkpoint_Work_Bound, with WorkBound = 64.
#include <fixy/Ctx.h>
#include <fixy/session/Checkpoint.h>

namespace neg_sess_checkpoint_work_bound_types {
struct Wire {};
}  // namespace neg_sess_checkpoint_work_bound_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_work_bound_types;
    using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
    using Follow = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_checkpoint_session<Decide, Follow, s::DefaultAbandonmentPolicy, 64>(ctx, Wire{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
