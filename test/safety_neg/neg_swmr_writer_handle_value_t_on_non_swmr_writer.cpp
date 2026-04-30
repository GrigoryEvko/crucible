// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D17 fixture — pins the constrained-extractor discipline on
// SwmrWriter.h.  swmr_writer_handle_value_t is constrained on
// `requires SwmrWriter<FnPtr>`; instantiating it on a non-SWMR-
// writer-shape function pointer must fail at the requires clause
// itself, NOT chain through swmr_writer_value_t (the D07 alias).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SwmrWriter.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::swmr_writer_handle_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
