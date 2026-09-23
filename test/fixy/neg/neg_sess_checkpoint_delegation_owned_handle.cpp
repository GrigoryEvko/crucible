// A rollback reverts the protocol and not the program.  An owning pointer
// to a session handle, sent after the checkpoint, stays with the peer
// after a rollback.  The rollback cannot recall the endpoint.  The old
// rule looked for the hand-off marker alone and admitted the pointer.
// The mint refuses it.

#include <fixy/session/Checkpoint.h>

#include <memory>

namespace s = fixy::session;

namespace {
struct Wire {};
using PeerEnd = s::SessionHandle<s::Recv<int, s::End>, Wire>;
}  // namespace

using Decide = s::Select<s::Commit<s::Send<std::unique_ptr<PeerEnd>, s::End>>, s::Roll>;
using Follow = s::Offer<s::Commit<s::Recv<std::unique_ptr<PeerEnd>, s::End>>, s::Roll>;

int main() {
    auto handle = s::mint_checkpoint_session<Decide, Follow>(Wire{});
    (void)handle;
    return 0;
}
