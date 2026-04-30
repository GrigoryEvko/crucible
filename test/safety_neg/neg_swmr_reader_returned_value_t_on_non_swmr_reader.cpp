// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D18 fixture — pins the constrained-extractor discipline.
// swmr_reader_returned_value_t is constrained on
// `requires SwmrReader<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SwmrReader.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::swmr_reader_returned_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
