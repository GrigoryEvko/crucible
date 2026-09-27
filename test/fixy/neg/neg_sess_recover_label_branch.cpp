// HandleFactory::recover enters a branch of an Offer with no word read.
// It admits only a branch that is no label, such as a crash branch,
// because no peer picks such a branch.  Branch 0 here is a label: the peer
// picks it with a word.  An endpoint that entered it with no word would
// read the word of the peer as the payload, so the call is refused.

#include <fixy/session/Crash.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace s = fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_recover_label_branch_types {
struct Peer {};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_recover_label_branch_types

using namespace neg_sess_recover_label_branch_types;

using Waits = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Peer>, s::End>>;

int main() {
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto head = s::mint_session<Waits>(ctx, Wire{});
    static_cast<void>(s::HandleFactory::recover<0>(ctx, std::move(head)));
    return 0;
}
