// Negative compile test.  This file must fail to compile.
//
// The payload of a send holds a token of Region in a member.  The payload
// walk sees the token, so the set of the sender must hold Region.  But the
// step can build only a marker or a bare token from the slots of the
// handle, and the caller builds this payload, with a token that comes from
// outside the slots.  So the gate of the session refuses a token in a
// member of a payload, and the mint refuses the protocol.  Before the slots,
// the mint admitted it, and a send took the token of the caller.
//
// Expected diagnostic: the permission flow of the protocol does not close.

#include <fixy/Ctx.h>
#include <fixy/session/Mint.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_token_in_a_member_of_a_payload_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
// The one root site of Region, so the member names the token of the root that
// the session holds, and the member is the one fault.
[[nodiscard]] inline auto mint_region_root() noexcept {
    return ::foundation::permissions::mint_permission_root<Region>();
}
struct CarriesToken {
    int value = 0;
    decltype(mint_region_root()) token;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using SendsCarrier = s::Send<CarriesToken, s::End>;
}  // namespace neg_sess_token_in_a_member_of_a_payload_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_token_in_a_member_of_a_payload_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_permissioned_session<SendsCarrier>(ctx, Wire{}, mint_region_root());
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
