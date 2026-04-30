// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D16 fixture — pins the constrained-extractor discipline.
// consumer_endpoint_region_tag_t is constrained on
// `requires ConsumerEndpoint<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ConsumerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using Tag = crucible::safety::extract::consumer_endpoint_region_tag_t<
        &::neg_witness_two_ints>;
    Tag const t{};
    (void)t;
    return 0;
}
