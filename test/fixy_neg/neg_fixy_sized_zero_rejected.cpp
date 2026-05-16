// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for grant::sized<0>.  Depth=0 is structurally meaningless
// (no observation depth at all); authors use accept_default_strict_for
// <dim::Size> for the unstated case.
//
// Pre-FIX: grant::sized<0> compiled.
// Post-FIX: hard compile error with "requires Depth > 0".

#include <crucible/fixy/Grant.h>

namespace cg = crucible::fixy::grant;

using BogusZeroDepth = cg::sized<0>;
BogusZeroDepth token{};

int main() { return 0; }
