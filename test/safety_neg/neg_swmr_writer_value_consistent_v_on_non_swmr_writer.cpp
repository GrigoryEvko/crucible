// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D17 fixture — pins the constrained-extractor discipline.
// swmr_writer_value_consistent_v is constrained on
// `requires SwmrWriter<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SwmrWriter.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    bool b = crucible::safety::extract::swmr_writer_value_consistent_v<
        &::neg_witness_two_ints>;
    (void)b;
    return 0;
}
