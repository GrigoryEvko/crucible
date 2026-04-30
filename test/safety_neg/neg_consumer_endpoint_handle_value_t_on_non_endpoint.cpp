// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D16 fixture — pins the constrained-extractor discipline on
// ConsumerEndpoint.h.  consumer_endpoint_handle_value_t is
// constrained on `requires ConsumerEndpoint<FnPtr>`; instantiating
// it on a non-endpoint-shape function pointer must fail at the
// requires clause itself, NOT chain through consumer_handle_value_t
// where the diagnostic would land far from the actual mistake.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ConsumerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::consumer_endpoint_handle_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
