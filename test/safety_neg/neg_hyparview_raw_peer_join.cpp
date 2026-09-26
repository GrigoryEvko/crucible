// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// join takes an overlay peer, and a raw identity does not convert to one.
// admit_hyparview_peer is the door that refuses a zero uuid.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto result = membership.join(raw);
    return result.has_value() ? 0 : 1;
}
