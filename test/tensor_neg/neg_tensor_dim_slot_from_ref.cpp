// Negative-compile test. This file must fail to compile.
//
// The conversion of a TensorDimArray::Slot to a TensorDim does not check
// the bound, because only the subscript of a block makes a Slot.  A Slot
// that a caller builds from a Ref of a block and an index is no door of the
// block, so the constructor of a Slot is private to the block.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    crucible::TensorDimArray block{};
    crucible::TensorDimArray::Slot const slot{::fixy::erase_brand(::fixy::to(block)), ::fixy::index<0>()};
    return crucible::raw_tensor_dim(slot) == 0 ? 0 : 1;
}
