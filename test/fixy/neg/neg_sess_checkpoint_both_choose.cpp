// Both sides decide between commit and rollback, and neither receives
// the other's choice.  The session is stuck at its first step.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;

int main() {
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_checkpoint_session<Decide, Decide>(ctx, Wire{});
    (void)handle;
    return 0;
}
