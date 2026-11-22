// Negative-compile test.  This file must fail to compile.
//
// A forward declaration before the header of a type cannot mark the type
// as a carrier of an authority.  Without -fannotations-on-definition-only
// (the seventh GCC patch), GCC takes the annotation of the declaration below
// as an annotation of crucible::StorageGeneration, but only in this unit:
// carries_annotation(^^StorageGeneration, ^^authority) is then true here, and
// false in each other unit.  The flag makes the annotation an error, and that
// error is the only error of the fixture.  If this fixture compiles, a unit
// can again forge the annotation that the gate of
// foundation/effects/Computation.h reads as an authority.

#include <foundation/Linear.h>

namespace crucible {
class[[= ::foundation::linear::authority{}]] StorageGeneration;
}  // namespace crucible

#include <crucible/Types.h>

int main() { return 0; }
