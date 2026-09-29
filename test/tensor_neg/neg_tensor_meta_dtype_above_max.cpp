// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidScalarType receives int8_t{99} in
// constant evaluation.  99 is far above the highest ScalarType enumerator,
// Float8_e4m3fnuz = 26.
//
// ValidScalarType is ::fixy::Refined<valid_scalar_type, int8_t>.  read_meta
// in Serialize.h reads the dtype byte through this gate, because
// element_size() marks every value that is not an enumerator unreachable,
// and an unchecked byte there is undefined behaviour.
//
// The companion fixture neg_tensor_meta_dtype_gap_value.cpp is an interior
// gap (14).  This one catches a predicate that is dropped entirely.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidScalarType bad =
        ::fixy::mint_refined<crucible::valid_scalar_type>(static_cast<std::int8_t>(99));
    (void)bad;
    return 0;
}
