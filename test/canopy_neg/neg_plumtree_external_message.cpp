// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// receive_message takes a message under the Gossiped tag.  A message
// under the External tag is a different type, and it does not convert.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(::foundation::effects::testing::init(), membership);
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto peer = crucible::canopy::admit_hyparview_peer(raw).value();
    (void)broadcast.add_eager_peer(peer);
    crucible::canopy::PlumtreeMessage message{};
    auto result = broadcast.receive_message(peer, ::fixy::mint_tagged<::fixy::tags::source::External>(message));
    return result.has_value() ? 0 : 1;
}
