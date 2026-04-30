// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D18 fixture — pins the constrained-extractor discipline.
// swmr_reader_value_consistent_v is constrained on
// `requires SwmrReader<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SwmrReader.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    bool b = crucible::safety::extract::swmr_reader_value_consistent_v<
        &::neg_witness_two_ints>;
    (void)b;
    return 0;
}
