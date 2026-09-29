// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidLayout receives int8_t{99} in constant
// evaluation.  Layout is dense, from Strided = 0 to SparseBsc = 5, and 99 is
// above the range.
//
// ValidLayout is ::fixy::Refined<valid_layout, int8_t>.  read_meta in
// Serialize.h reads the layout byte through this gate.  The layout feeds the
// node content hash, so an unchecked byte would change the identity of a
// node without any other symptom.
//
// The companion fixture neg_tensor_meta_layout_below_min.cpp is the value
// -1.  This one catches a predicate that is dropped entirely.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidLayout bad = ::fixy::mint_refined<crucible::valid_layout>(static_cast<std::int8_t>(99));
    (void)bad;
    return 0;
}
