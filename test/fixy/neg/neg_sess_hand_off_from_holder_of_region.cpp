// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A session that holds the region Region sends an endpoint whose set holds
// Region too.  The endpoint keeps the token of Region in its slots, and the
// slots of the sender hold a token of Region, so the sender makes two
// claims on one region before the send.  The send of the hand-off checks
// that the tags of the endpoint name no region of the set of the sender,
// so the mint refuses the protocol.  Before the slots, the hand-off moved
// the tag of the sender, and this file compiled.
//
// Expected diagnostic: the permission flow of the protocol does not close.

#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Mint.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_hand_off_from_holder_of_region_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<s::Transferable<int, Region>, s::End>;
using Parcel =
    s::DelegatedSession<Inner, Wire, s::DefaultAbandonmentPolicy, ::foundation::permissions::PermSet<Region>>;
using GivesParcel = s::Send<Parcel, s::End>;
}  // namespace neg_sess_hand_off_from_holder_of_region_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_hand_off_from_holder_of_region_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_permissioned_session<GivesParcel>(ctx, Wire{},
                                                          ::foundation::permissions::mint_permission_root<Region>());
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
