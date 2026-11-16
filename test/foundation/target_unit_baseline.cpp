// The unit of test_target_units for the baseline target of x86-64.  The
// build compiles this file with -march=x86-64.

#include "target_unit_body.h"

namespace crucible::test {

constinit TargetUnitBodies const baseline_unit_bodies = bodies_of_this_unit();

}  // namespace crucible::test
