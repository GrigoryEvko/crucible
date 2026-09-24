// A rollback reverts the protocol and not the program.  A permission
// sent after the checkpoint would be asked for again after a rollback,
// while the receiver still holds it, so the mint refuses the payload.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

struct Region {};
using Decide = s::Select<s::Commit<s::Send<s::Transferable<int, Region>, s::End>>, s::Roll>;
using Follow = s::Offer<s::Commit<s::Recv<s::Transferable<int, Region>, s::End>>, s::Roll>;

int main() {
    auto handle = s::mint_checkpoint_session<Decide, Follow>(Wire{});
    (void)handle;
    return 0;
}
