// A Roll outside a choice is a rollback that one side takes alone,
// with no label to tell the peer.

#include <fixy/session/Checkpoint.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_checkpoint_session<s::Send<int, s::Roll>, s::Recv<int, s::Roll>>(ctx, Wire{});
    (void)handle;
    return 0;
}
