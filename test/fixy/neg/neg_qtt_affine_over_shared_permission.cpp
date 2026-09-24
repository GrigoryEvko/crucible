// Affine<SharedPermission<Tag>> is the unsound half of the pair: the
// shared token carries an obligation, and the Affine wrapper makes
// discharging it optional.
//
// This fixture covers the second template in the recogniser's family.
// Its sibling neg_qtt_linear_over_permission.cpp covers the first, so
// removing either name from the concept body leaves one of the two
// fixtures compiling, which is the failure the pair exists to catch.
//
// The second required diagnostic is the compiler's rendering of the
// shared-permission specialization inside the Qtt instantiation, which
// the source does not spell.

#include <fixy/Qtt.h>

struct NegQttSharedTag;

using AffineOverShared = fixy::Affine<::foundation::permissions::SharedPermission<NegQttSharedTag>>;

static_assert(sizeof(AffineOverShared) > 0);

int main() { return 0; }
