// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D15 fixture — pins the constrained-extractor discipline.
// producer_endpoint_region_tag_t is constrained on
// `requires ProducerEndpoint<FnPtr>`; instantiating it on a
// non-endpoint-shape function pointer must fail at the requires
// clause itself, NOT chain through owned_region_tag_t.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ProducerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using Tag = crucible::safety::extract::producer_endpoint_region_tag_t<
        &::neg_witness_two_ints>;
    Tag const t{};
    (void)t;
    return 0;
}
