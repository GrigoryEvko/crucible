// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D17 fixture — pins the constrained-extractor discipline.
// swmr_writer_published_value_t is constrained on
// `requires SwmrWriter<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SwmrWriter.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::swmr_writer_published_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
