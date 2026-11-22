// Negative compile test.  This file must fail to compile.
//
// A session that holds the region Region receives an endpoint whose set
// holds Region too.  The endpoint keeps the token of Region in its slots,
// and the slots of the recipient hold a token of Region, so the recipient
// would make two claims on one region.  The receive of the hand-off checks
// that the tags of the endpoint name no region of the set of the
// recipient, so the mint refuses the protocol.
//
// Expected diagnostic: the permission flow of the protocol does not close.

#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Mint.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_hand_off_to_holder_of_region_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
// The brand of the root of the endpoint, another root of Region.  One tag is
// one region of a set, so the claims meet.
struct ParcelBrand {};
using Inner = s::Send<s::Transferable<int, Region, ParcelBrand>, s::End>;
using Parcel =
    s::DelegatedSession<Inner, Wire, s::DefaultAbandonmentPolicy,
                        ::foundation::permissions::PermSet<::foundation::permissions::Permission<Region, ParcelBrand>>>;
using TakesParcel = s::Recv<Parcel, s::End>;
}  // namespace neg_sess_hand_off_to_holder_of_region_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_hand_off_to_holder_of_region_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_permissioned_session<TakesParcel>(ctx, Wire{},
                                                          ::foundation::permissions::mint_permission_root<Region>());
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
