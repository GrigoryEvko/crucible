// Negative-compile test. This file must fail to compile.
//
// bit_cast of the Scalar family copies the bits of a plain value into a
// bit valid type with no check.  An array of eight int64_t would give a
// TensorDimArray whose lanes hold any value, and a lane of the block then
// converts to a TensorDim past the bound.  The block carries the annotation
// no_start_over_bytes, so it is not plain, and bit_cast refuses it.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

#include <cstdint>

int main() {
    ::fixy::FixedArray<std::int64_t, crucible::kMaxTensorNDim> const lanes{};
    crucible::TensorDimArray const block = ::fixy::bit_cast<crucible::TensorDimArray>(lanes);
    return crucible::raw_tensor_dim(block[0]) == 0 ? 0 : 1;
}
