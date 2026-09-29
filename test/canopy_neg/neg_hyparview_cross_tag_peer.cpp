// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// join takes an overlay peer.  An identity under the External source tag
// is untrusted input, and it is not an overlay peer.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    crucible::cog::CogIdentity raw{.uuid = crucible::cog::Uuid{1, 2}};
    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(raw);
    auto result = membership.join(external);
    return result.has_value() ? 0 : 1;
}
