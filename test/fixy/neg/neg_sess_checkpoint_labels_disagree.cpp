// The two sides disagree about which label is the rollback.  Label 1 is
// a Roll for the decider and a Commit for the follower, so each side
// would believe a different thing happened.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
using Swapped = s::Offer<s::Roll, s::Commit<s::Recv<int, s::End>>>;

int main() {
    auto handle = s::mint_checkpoint_session<Decide, Swapped>(Wire{});
    (void)handle;
    return 0;
}
