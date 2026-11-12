// The mint reads the name of a predicate, and the implication relation
// reads it too.  This file tries to forge a refinement over in_range<1,
// 4096> that holds zero: it writes an explicit specialization of the
// class template InRange whose call operator admits each value.  The seal
// of the mint finds the class outside fixy/Predicates.h and stops the
// build.

#include <fixy/Refined.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges InRange<1, 4096> with a call operator that admits each value")
template <>
struct fixy::InRange<1, 4096> {
    template <class V>
    constexpr bool operator()(V) const noexcept {
        return true;
    }
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

constexpr auto forged = fixy::mint_refined<fixy::in_range<1, 4096>>(0);

int main() { return forged.value(); }
