// Negative compile test.  This file must fail to compile.
//
// The deciding side loops over a Select whose first two labels are one type,
// a send of an int that goes back to the loop.  The following side accepts
// those two labels with two different branches: the first goes back to the
// loop, and the second enters a copy of the loop.  So the two equal labels
// lead to two different continuations, and the label names no one
// continuation.  The checkpoint walk treats two labels as one edge only when
// the labels and their continuations are equal, and it refuses this choice
// as ambiguous.  Each configuration of the pair can move, so the walk that
// admitted each label found the pair compliant.
//
// Expected diagnostic: the verdict of the pair is AmbiguousLabels.
#include <fixy/Ctx.h>
#include <fixy/session/Checkpoint.h>

#include <utility>

namespace neg_sess_checkpoint_ambiguous_labels_types {
namespace s = ::fixy::session;
struct Wire {};
using Again = s::Send<int, s::Continue>;
using Decide = s::Loop<s::Select<Again, Again, s::End>>;
using Inner = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<int, s::Continue>, s::End>>;
using Follow = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<int, Inner>, s::End>>;
}  // namespace neg_sess_checkpoint_ambiguous_labels_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_ambiguous_labels_types;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_checkpoint_session<Decide, Follow>(ctx, Wire{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
