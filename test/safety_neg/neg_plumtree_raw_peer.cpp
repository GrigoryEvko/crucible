// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A link comes from an overlay peer, and a raw identity does not convert
// to one.
#include <crucible/canopy/Plumtree.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    auto broadcast = crucible::canopy::mint_plumtree<4, 8>(::foundation::effects::testing::init(), membership);
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto result = broadcast.add_eager_peer(raw);
    return result.has_value() ? 0 : 1;
}
