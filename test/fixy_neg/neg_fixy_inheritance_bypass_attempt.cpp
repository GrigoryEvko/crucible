// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-A-PLUS-1 — Closes Phase A's architectural-limit cheat #2.
// Pre-A-PLUS-1: `struct fake : accept_default_strict_for<dim::X> {};`
// would compile and forge engagement via inherited `relaxes` member.
// Post-A-PLUS-1: every shipped grant tag is `final` (via
// safety/NotInherited.h discipline).  The derivation attempt below
// is now a COMPILE ERROR at the inheritance site, BEFORE IsAccepted
// ever sees the forged type.
//
// GCC's diagnostic on `final`-base derivation contains the literal
// string "final" — the PASS_REGULAR_EXPRESSION on the test matches
// this.

#include <crucible/fixy/Grant.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

// This single line must fail compilation with a "final" diagnostic.
struct ForgeAttempt : cf::accept_default_strict_for<cd::Usage> {};

int main() { return 0; }
