// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-D PROFILE-PIN — strict_fn must REJECT an empty Grants pack even
// when CRUCIBLE_FIXY_SKETCH is defined.  The profile-pin's WHOLE POINT
// is that production-critical bindings cannot be silenced by a
// sketch-mode build.
//
// Sketch defaults `IsAcceptedSelected` to a permissive concept, but
// `IsAcceptedStrict` stays HARD — production code that wrapped in
// strict_fn / IsAcceptedStrict never gets silenced.

#define CRUCIBLE_FIXY_SKETCH 1

#include <crucible/fixy/Profile.h>

namespace cf = crucible::fixy;

// IsAcceptedSelected becomes permissive — even empty pack satisfies.
static_assert(cf::IsAcceptedSelected<>,
    "Under CRUCIBLE_FIXY_SKETCH the profile selector must be permissive.");

// But IsAcceptedStrict must STILL fail on empty pack.
static_assert(cf::IsAcceptedStrict<>, "FixyNotEngaged_Type");

int main() { return 0; }
