// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// HS14 fixture for grant::complexity_linear<0>.  N=0 is O(0) which is
// structurally meaningless; O(1) is grant::complexity_constant.
//
// Pre-FIX: grant::complexity_linear<0> compiled.
// Post-FIX: hard compile error with "requires N > 0".

#include <crucible/fixy/Grant.h>

namespace cg = crucible::fixy::grant;

using BogusZeroN = cg::complexity_linear<0>;
BogusZeroN token{};

int main() { return 0; }
