// The two factors of a matrix product take two roles of Side, so a call with
// the two factors in the other order does not compile.

#include <foundation/core/Product.h>

struct SwapBrand final {};

inline void multiply_swapped(::foundation::core::StridedViewMut<float, 2, SwapBrand>& out,
                             ::foundation::core::StridedView<float const, 2> left,
                             ::foundation::core::StridedView<float const, 2> right) {
    static_cast<void>(::foundation::core::multiply_by_tree(
        out, ::foundation::core::side_as<::foundation::core::side::RightFactor>(right),
        ::foundation::core::side_as<::foundation::core::side::LeftFactor>(left)));
}

int main() {}
