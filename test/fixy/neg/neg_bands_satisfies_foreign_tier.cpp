// satisfies_v compares a band against a tier of its own outer chain.  A
// tier from a different enum is a different axis, so the constraint
// that the required tier has the band's tier type is not satisfied:
// a DetSafe band cannot be asked if it satisfies a HotPath tier.

#include <fixy/Bands.h>

int main() {
    using PureInt = fixy::det_safe::Pure<int>;
    constexpr bool admitted = fixy::satisfies_v<PureInt, fixy::HotPathTier_v::Hot>;
    return admitted ? 0 : 1;
}
