// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-D PROFILE-PIN — strict_fn rejects an empty Grants pack with
// the same first-dim diagnostic fn<> does.  strict_fn is a profile-
// pinned alias of fn<>; pinning must not introduce a soundness gap.
//
// We exercise the strict gate through Profile.h's IsAcceptedStrict
// concept directly — IsAcceptedStrict<> must equal IsAccepted<>,
// and IsAccepted<> with no grants fails at dim::Type.

#include <crucible/fixy/Profile.h>

namespace cf = crucible::fixy;

static_assert(cf::IsAcceptedStrict<>, "FixyNotEngaged_Type");

int main() { return 0; }
