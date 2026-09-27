// HandleFactory::recover opens the rest of a session at a crash branch
// with no word read.  It admits only a handle whose permission set is
// empty, as a mint admits a protocol at its start with no token.  This
// handle holds the token of Region at the Offer, so the call is refused.

#include <fixy/session/Crash.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace s = fixy::session;
namespace eff = ::foundation::effects;

namespace neg_sess_recover_permission_held_types {
struct Peer {};
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_recover_permission_held_types

using namespace neg_sess_recover_permission_held_types;

// The crash branch alone closes from the empty set, so the one fault is the
// token that the handle holds.
using Waits = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Peer>, s::End>>;

int main() {
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto [head, hold] =
        s::mint_permissioned_session<Waits>(ctx, Wire{}, ::foundation::permissions::mint_permission_root<Region>());
    static_cast<void>(s::HandleFactory::recover<1>(ctx, std::move(head)));
    static_cast<void>(hold);
    return 0;
}
