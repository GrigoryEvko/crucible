// A rollback reverts the protocol and not the program.  A delegated
// endpoint sent after the checkpoint is held by the peer, and a rollback
// cannot recall it, so the mint refuses the payload.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Wire {};
}  // namespace

using Inner = s::DelegatedSession<s::Recv<int, s::End>, fp::EmptyPermSet>;
using Decide = s::Select<s::Commit<s::Send<Inner, s::End>>, s::Roll>;
using Follow = s::Offer<s::Commit<s::Recv<Inner, s::End>>, s::Roll>;

int main() {
    auto handle = s::mint_checkpoint_session<Decide, Follow>(Wire{});
    (void)handle;
    return 0;
}
