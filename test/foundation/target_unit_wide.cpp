// The unit of test_target_units for the target of x86-64-v4, with AVX-512.
// The build compiles this file with -march=x86-64-v4.

#include "target_unit_body.h"

namespace crucible::test {

constinit TargetUnitBodies const wide_unit_bodies = bodies_of_this_unit();

}  // namespace crucible::test
