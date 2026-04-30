// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D15 fixture — pins the constrained-extractor discipline on
// ProducerEndpoint.h.  producer_endpoint_handle_value_t is
// constrained on `requires ProducerEndpoint<FnPtr>`; instantiating
// it on a non-endpoint-shape function pointer must fail at the
// requires clause itself, NOT chain through producer_handle_value_t
// where the diagnostic would land far from the actual mistake.
//
// Concrete bug-class this catches: a refactor that drops the
// `requires ProducerEndpoint<FnPtr>` constraint on
// producer_endpoint_handle_value_t (or any of its FOUR siblings —
// region_tag_t / region_value_t / value_consistent_v) would let
// the alias silently chain through `param_type_t<FnPtr, 0>`
// followed by `producer_handle_value_t`, producing a constraint
// cascade that lands deep inside producer_handle_value_t's requires
// clause rather than at the producer-endpoint-shape boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/ProducerEndpoint.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    // arity 2 BUT param 0 is int (not a producer handle) and
    // param 1 is int (not an OwnedRegion).
    // ProducerEndpoint<&neg_witness_two_ints> is false.
    using V = crucible::safety::extract::producer_endpoint_handle_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
