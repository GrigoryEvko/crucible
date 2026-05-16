// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A6 — every dim unengaged (empty Grants pack).  IsAccepted's
// first per-dim clause (EngagedFor<dim::Type, ...>) fails on an
// empty fold, so the FIRST diagnostic emitted is FixyNotEngaged_Type.
//
// This demonstrates that the discipline truly rejects-by-default —
// the absence of grants is not implicit acceptance.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;

static_assert(cf::IsAccepted<>, "FixyNotEngaged_Type");

int main() { return 0; }
