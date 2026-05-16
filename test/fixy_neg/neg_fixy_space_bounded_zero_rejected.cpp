// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for grant::space_bounded<0>.  N=0 collapses to "no
// allocation permitted" which IS the strict default (space::Zero);
// engaging dim::Space with a degenerate bound masks the author's
// intent.  Authors use accept_default_strict_for<dim::Space> for the
// stack-only case.
//
// Pre-FIX: grant::space_bounded<0> compiled, silently engaged Space.
// Post-FIX: hard compile error with "requires N > 0".

#include <crucible/fixy/Grant.h>

namespace cg = crucible::fixy::grant;

using BogusZeroBytes = cg::space_bounded<0>;
BogusZeroBytes token{};

int main() { return 0; }
