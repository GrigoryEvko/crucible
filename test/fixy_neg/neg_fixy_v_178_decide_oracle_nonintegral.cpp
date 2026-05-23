// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-178 fixy::decide::oracle guarantee fixture
// (class: NON-INTEGRAL ORACLE ARGUMENT).
//
// no_overflow_mul_oracle<T> is constrained `std::integral T`
// (safety/DecideOracle.h) — it widens to int64/int128 and bounds-
// checks the product, which only makes sense for integral T.  Calling
// it with `double` arguments deduces T = double, which fails
// std::integral, so no candidate matches.  The fixy::decide::oracle::
// re-export preserves the substrate constraint: a floating-point
// oracle invocation through the umbrella reds.
//
// Distinct mismatch class from the signature_traits fixture (non-fn
// reflection) and the GradedExtract fixture (non-Graded alias arg).
//
// Expected diagnostic: no matching function / constraints not
// satisfied / integral.

#include <crucible/fixy/Decide.h>

namespace oracle = crucible::fixy::decide::oracle;

// double is not std::integral → no_overflow_mul_oracle has no viable
// candidate.
bool probe() {
    return oracle::no_overflow_mul_oracle(1.0, 2.0);
}

int main() { return 0; }
