// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidScalarType receives int8_t{14} in
// constant evaluation.  14 is an interior gap of the sparse ScalarType set:
// Bool is 11 and BFloat16 is 15, and 12 thru 14 are not enumerators.
//
// ValidScalarType is ::fixy::Refined<valid_scalar_type, int8_t>.  read_meta
// in Serialize.h reads the dtype byte through this gate, because
// element_size() marks every value that is not an enumerator unreachable.
//
// The companion fixture neg_tensor_meta_dtype_above_max.cpp is the wide
// miss (99).  This one catches a predicate relaxed to a plain range check,
// which admits the gaps.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidScalarType bad =
        ::fixy::mint_refined<crucible::valid_scalar_type>(static_cast<std::int8_t>(14));
    (void)bad;
    return 0;
}
