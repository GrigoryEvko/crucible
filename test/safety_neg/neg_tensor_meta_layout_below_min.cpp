// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidLayout receives int8_t{-1} in constant
// evaluation.  Layout is dense, from Strided = 0 to SparseBsc = 5, and -1 is
// below the range.
//
// ValidLayout is ::fixy::Refined<valid_layout, int8_t>.  read_meta in
// Serialize.h reads the layout byte through this gate, because the layout
// feeds the node content hash.
//
// The companion fixture neg_tensor_meta_layout_above_max.cpp is the wide
// miss (99).  This one catches a naive bounded_above<5> over the signed
// byte: -1 <= 5 holds, so that bound would admit it.  The named cases of
// the predicate refuse it.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidLayout bad = ::fixy::mint_refined<crucible::valid_layout>(static_cast<std::int8_t>(-1));
    (void)bad;
    return 0;
}
