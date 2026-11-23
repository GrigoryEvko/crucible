// Negative-compile test. This file must fail to compile.
//
// The conversion of a TensorDimArray::ConstSlot to a TensorDim does not
// check the bound, because only the subscript of a const block makes a
// ConstSlot, and each lane of a block holds a TensorDim.  A ConstSlot of a
// Ref to any int64_t would give a TensorDim past the bound, so the
// constructor of a ConstSlot is private to the block.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

#include <cstdint>

int main() {
    std::int64_t const huge = ::fixy::max_of<std::int64_t>;
    crucible::TensorDim const dim = crucible::TensorDimArray::ConstSlot{::fixy::erase_brand(::fixy::to(huge))};
    return dim.value() > crucible::kMaxTensorDimExtent ? 0 : 1;
}
