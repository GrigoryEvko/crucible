// A refinement witness whose predicate has no call operator.
//
// refined_with<Pred> states that Pred holds for the payload, and the mint
// of the binding checks that claim with a call of Pred.  A class with no
// call operator states no fact about any value, so a hot binding that
// names it silences H002 with a witness that refines nothing.  The gate
// refuses the binding at tier 5.

#include <fixy/Fn.h>

struct parser_bounds_proved final {};

using Refused = ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                           ::fixy::atom::refined_with<parser_bounds_proved>>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
