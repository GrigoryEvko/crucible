// The pole of a Right axis grants no right.
//
// A Right axis carries a right that the binding claims for its body.  A
// pole that claims nothing puts no bound on the body, so on a Right axis
// it grants each right to each binding that says nothing.  The
// rule refuses such a pole.

#include <fixy/Axis.h>

static_assert(::fixy::PoleFitsClaim<::fixy::Claim::Right, ::fixy::pole::Unconstrained<::fixy::Axis::Effect>>,
              "a Right pole that claims nothing is the weakest claim");

int main() { return 0; }
