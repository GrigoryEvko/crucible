// The unit of test_crucible_target_units for the target of x86-64-v4, with
// AVX-512.  The build compiles this file with -march=x86-64-v4.

#include "crucible_target_unit_body.h"

namespace crucible::test {

constinit CrucibleTargetUnit const wide_crucible_unit = record_of_this_unit();

}  // namespace crucible::test
