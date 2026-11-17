// The unit of test_crucible_target_units for the baseline level x86-64-v3,
// the floor of the tree.  The build compiles this file with -march=x86-64-v3.

#include "crucible_target_unit_body.h"

namespace crucible::test {

constinit CrucibleTargetUnit const baseline_crucible_unit = record_of_this_unit();

}  // namespace crucible::test
