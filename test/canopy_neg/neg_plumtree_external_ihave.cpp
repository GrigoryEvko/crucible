// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// receive_ihave takes a repair summary under the Gossiped tag.  A summary
// under the External tag is a different type, and it does not convert.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(::foundation::effects::testing::init(), membership);
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto peer = crucible::canopy::admit_hyparview_peer(raw).value();
    (void)broadcast.add_lazy_peer(peer);
    crucible::canopy::PlumtreeIHave<8> ihave{};
    auto result = broadcast.receive_ihave(peer, ::fixy::mint_tagged<::fixy::tags::source::External>(ihave));
    return result.has_value() ? 0 : 1;
}
