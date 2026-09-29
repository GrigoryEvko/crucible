// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// apply_shuffle takes a shuffle that a peer sent, under the Gossiped tag.
// A raw shuffle does not convert to one.
#include <crucible/canopy/HyParView.h>

int main() {
    auto membership = crucible::canopy::mint_hyparview<4, 8>(::foundation::effects::testing::init());
    crucible::canopy::HyParViewShuffle<8> raw{};
    auto result = membership.apply_shuffle(raw);
    return result.has_value() ? 0 : 1;
}
