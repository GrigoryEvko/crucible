// A unit of test_core_elementwise: the test of Sum, Quotient and
// FusedMultiplyAdd of floats.  elementwise_rig.h tells why the tests compile
// in more than one unit.

#include "elementwise_rig.h"

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace crucible::test {

void test_the_float_single_rounds_are_exact() { test_the_single_rounds_are_exact<float>(kTest); }

}  // namespace crucible::test
