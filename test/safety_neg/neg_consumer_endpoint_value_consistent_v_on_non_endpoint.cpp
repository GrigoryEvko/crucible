// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D16 fixture — pins the constrained-extractor discipline.
// consumer_endpoint_value_consistent_v is constrained on
// `requires ConsumerEndpoint<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ConsumerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    bool b = crucible::safety::extract::consumer_endpoint_value_consistent_v<
        &::neg_witness_two_ints>;
    (void)b;
    return 0;
}
