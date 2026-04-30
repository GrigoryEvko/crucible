// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D15 fixture — pins the constrained-extractor discipline.
// producer_endpoint_value_consistent_v is constrained on
// `requires ProducerEndpoint<FnPtr>`; instantiating it on a non-
// endpoint-shape function pointer must fail at the requires clause.
//
// Without the requires clause, the variable template would yield
// `false` (because handle_value_t and region_value_t both fail to
// extract via their own requires-clauses, but the v_consistent_v
// itself would still attempt the std::is_same_v check on the
// substitution-failed expressions).  Cleanly rejecting at the
// outer requires-clause boundary keeps the diagnostic readable.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ProducerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    bool b = crucible::safety::extract::producer_endpoint_value_consistent_v<
        &::neg_witness_two_ints>;
    (void)b;
    return 0;
}
