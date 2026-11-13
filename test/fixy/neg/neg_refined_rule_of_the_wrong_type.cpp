// A rule of the relation is a function of type rule_signature.  A
// function of another type in admitted_implications is no rule, and the
// walk would skip it.  The natural slip is a rule that returns bool, and
// it would then compile and decide nothing.  The edge walk of the header
// refuses it instead.
//
// The function is planted before the header, because the relation reads
// the namespace as it stands where the header defines it.  Then the
// fixture calls the walk.

#include <foundation/Quarantine.h>

#include <meta>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::refined::admitted_implications {

consteval bool returns_bool(std::meta::info premise, std::meta::info conclusion) { return premise != conclusion; }

}  // namespace fixy::refined::admitted_implications
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include <fixy/Predicates.h>

static_assert(::fixy::detail::refined_edge_walk::every_edge_holds());

int main() { return 0; }
