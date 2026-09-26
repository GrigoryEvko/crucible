// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// apply_shuffle takes a shuffle under the Gossiped tag.  A shuffle under
// the External tag is a different type, and it does not convert.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    crucible::canopy::HyParViewShuffle<8> raw{};
    auto result = membership.apply_shuffle(::fixy::mint_tagged<::fixy::tags::source::External>(raw));
    return result.has_value() ? 0 : 1;
}
