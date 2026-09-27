// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Commit is a marker that the registry marks as not plain.  The
// well-formedness algebra refuses a combinator that is not plain at every
// depth, so a plain mint refuses a protocol that commits below a step.
//
// Expected diagnostic: the mint refuses the protocol because it is not
// well-formed.
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

namespace neg_sess_marker_not_plain_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_marker_not_plain_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_marker_not_plain_types;
    using Commits = s::Send<int, s::Commit<s::Recv<int, s::End>>>;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto handle = s::mint_session<Commits>(ctx, Wire{});
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
