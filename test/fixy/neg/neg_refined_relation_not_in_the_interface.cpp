// fixy/Refined.h gives the refinement and its mints, and not the
// implication relation of fixy/PredicateImplies.h.  So a unit that only
// mints and reads a refinement does not compile the relation.  This file
// names the relation with fixy/Refined.h alone, and the name has no
// declaration there.

#include <fixy/Refined.h>

static_assert(fixy::PredicateImplies<fixy::positive, fixy::non_zero>);

int main() { return 0; }
