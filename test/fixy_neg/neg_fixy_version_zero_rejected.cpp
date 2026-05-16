// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for grant::version<0>.  V=0 is structurally meaningless:
// substrate strict default is 1, and a literal v=0 in a federation-cache
// key would collide with "unset".  Authors who want the strict default
// use accept_default_strict_for<dim::Version>; explicit grant::version
// requires V > 0.
//
// Pre-FIX: grant::version<0> compiled, silently engaged dim::Version
// with a meaningless value, polluted federation cache keys.
// Post-FIX: hard compile error at the grant tag instantiation site
// with the diagnostic "requires V > 0".

#include <crucible/fixy/Grant.h>

namespace cg = crucible::fixy::grant;

// Must fire `static_assert(V > 0, ...)` inside grant::version.
using BogusZero = cg::version<0>;
BogusZero token{};

int main() { return 0; }
