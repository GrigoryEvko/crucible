// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for grant::stale_to<0>.  TauMax=0 means τ ≤ 0 which IS
// stale::Fresh (the strict default); authors who want Fresh use
// accept_default_strict_for<dim::Staleness>.
//
// Pre-FIX: grant::stale_to<0> compiled.
// Post-FIX: hard compile error with "requires TauMax > 0".

#include <crucible/fixy/Grant.h>

namespace cg = crucible::fixy::grant;

using BogusZeroTau = cg::stale_to<0>;
BogusZeroTau token{};

int main() { return 0; }
