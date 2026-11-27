// Negative-compile test. This file must fail to compile.
//
// Violation: a caller builds a TensorRank from a checked rank and gives it to
// a descriptor.  The rank is at most kMaxTensorNDim, but no lane agrees with
// it, so the constructor of TensorRank from a rank is private to TensorShape.

#include <crucible/TensorMeta.h>
#include <fixy/Refined.h>

int main() {
    crucible::TensorMeta const meta{
        .ndim =
            crucible::TensorRank{::fixy::mint_refined<::fixy::bounded_above<crucible::kMaxTensorNDim>>(uint8_t{2})}};
    return meta.ndim;
}
