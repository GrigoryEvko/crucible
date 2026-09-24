// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// tensor_dim gates every size and stride by bounded_above<kMaxTensorDimExtent>,
// where the bound is INT64_MAX / 16 for the widest element.  It states the
// bound as a contract assertion ahead of the checked mint, so the boundary
// value plus one violates the contract in constant evaluation, and the
// constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr auto bad = crucible::tensor_dim(crucible::kMaxTensorDimExtent + std::int64_t{1});
    (void)bad;
    return 0;
}
