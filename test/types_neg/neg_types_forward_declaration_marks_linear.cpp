// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A forward declaration before the header of a type cannot mark the type
// linear.  Without -fannotations-on-definition-only (the seventh GCC patch),
// GCC takes the annotation of the declaration below as an annotation of
// crucible::OpDepth, but only in this unit: is_marked_linear(^^OpDepth) is
// then true here, and false in each other unit.  The flag makes the
// annotation an error, and that error is the only error of the fixture.  If
// this fixture compiles, a unit can again give a type of a header a marker
// that the definition does not have.

#include <foundation/Linear.h>

namespace crucible {
class[[= ::foundation::linear::linear{}]] OpDepth;
}  // namespace crucible

#include <crucible/Types.h>

int main() { return 0; }
