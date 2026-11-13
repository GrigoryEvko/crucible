// An implication edge added to the relation after fixy/Predicates.h.
//
// Zero is non-negative and not positive, so the edge is unsound.  The
// file opens admitted_implications again and declares it, as a file
// could after it includes the header.  The relation holds a seal that
// counts its members at the foot of the header, and each query reads
// the seal first.  The query below asks a pair that the header does not
// ask, so it stops the build rather than reach the late edge through
// the chain from the range to non_negative.

#include <fixy/Predicates.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::refined::admitted_implications {

inline constexpr ::foundation::fail_closed::edge<predicate_t<::fixy::non_negative>, predicate_t<::fixy::positive>>
    late_edge{};

}  // namespace fixy::refined::admitted_implications
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::PredicateImplies<fixy::in_range<0, 7>, fixy::positive>);

int main() { return 0; }
