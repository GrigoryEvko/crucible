// A rule of the relation is a function of type rule_signature, and the
// walk over admitted_implications calls each one.
//
// A class in that namespace is no rule, and the walk would skip it.  A
// class that a file writes there to extend the relation would then
// compile and decide nothing.  The edge walk of the header refuses it
// instead.
//
// The class is planted before the header, because the relation reads
// the namespace as it stands where the header defines it.  Then the
// fixture calls the walk.

namespace fixy::refined::admitted_implications {

// Neither an edge nor a rule.
struct not_a_rule {};

}  // namespace fixy::refined::admitted_implications

#include <fixy/Refined.h>

static_assert(::fixy::detail::refined_edge_walk::every_edge_holds());

int main() { return 0; }
