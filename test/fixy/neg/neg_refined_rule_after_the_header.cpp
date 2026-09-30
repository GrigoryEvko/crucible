// A rule added to the relation after fixy/Refined.h.
//
// The rule decides non_negative ⇒ non_zero, which zero refutes.  It is a
// function and not an edge, so a count of edges alone would not see it.
// The seal counts every member of admitted_implications, and each query
// reads the seal.  The query below asks a pair that the header does not
// ask, so it stops the build.

#include <fixy/Refined.h>

#include <meta>

namespace fixy::refined::admitted_implications {

consteval rule_verdict late_rule(std::meta::info premise, std::meta::info conclusion) {
    return {.admits = premise == (^^IsNonNegative) && conclusion == (^^IsNonZero)};
}

}  // namespace fixy::refined::admitted_implications

static_assert(!fixy::PredicateImplies<fixy::bounded_below<2>, fixy::non_zero>);

int main() { return 0; }
