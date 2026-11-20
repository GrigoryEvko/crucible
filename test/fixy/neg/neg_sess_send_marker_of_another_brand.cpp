// A marker moves the token of the brand that it names.  The session holds the
// token of its root, and the protocol names another brand of the same tag, so
// the set of the sender holds no token that the marker moves, and the mint
// refuses the protocol.  A marker that names any brand of its tag would let a
// token of one root stand for a token of another root.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_marker_of_another_brand_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
// The brand of another root of Region.
struct OtherRootBrand {};

using Message = s::Transferable<int, Region, OtherRootBrand>;

struct Mailbox {
    ::fixy::FixedList<Message, 1> held{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace neg_sess_send_marker_of_another_brand_types

int main() {
    namespace types = neg_sess_send_marker_of_another_brand_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto sender = types::s::mint_permissioned_session<types::s::Send<types::Message, types::s::End>>(
        ctx, types::Mailbox{}, types::fp::mint_permission_root<types::Region>());
    static_cast<void>(sender);
    return 0;
}
