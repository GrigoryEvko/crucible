// The implication relation reasons from the name of a predicate and its
// arguments: in_range<1, 4096> implies positive, because one is a floor
// above zero.  This file specializes InRange<1, 4096> so that it admits
// zero, and then asks the relation for that step.  The seal of the
// relation finds the class outside fixy/Predicates.h and stops the build,
// so no gate reads a claim that the specialized class refutes.

#include <fixy/Predicates.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges InRange<1, 4096> to admit zero")
template <>
struct fixy::InRange<1, 4096> {
    template <class V>
    constexpr bool operator()(V) const noexcept {
        return true;
    }
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::PredicateImplies<fixy::in_range<1, 4096>, fixy::positive>);

int main() { return 0; }
