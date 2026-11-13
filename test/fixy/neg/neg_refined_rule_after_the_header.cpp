// A rule added to the relation after fixy/Predicates.h.
//
// The rule decides non_negative ⇒ non_zero, which zero refutes.  It is a
// function and not an edge, so a count of edges alone would not see it.
// The seal counts every member of admitted_implications, and each query
// reads the seal.  The query below asks a pair that the header does not
// ask, so it stops the build.

#include <fixy/Predicates.h>
#include <foundation/Quarantine.h>

#include <meta>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::refined::admitted_implications {

consteval rule_verdict late_rule(std::meta::info premise, std::meta::info conclusion) {
    return {.admits = premise == (^^IsNonNegative) && conclusion == (^^IsNonZero)};
}

}  // namespace fixy::refined::admitted_implications
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(!fixy::PredicateImplies<fixy::bounded_below<2>, fixy::non_zero>);

int main() { return 0; }
