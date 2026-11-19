// The unit of test_core_product for the level x86-64-v3.  The build compiles
// this file with -march=x86-64-v3.

#include "product_units.h"

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace crucible::test {

ProductDigests product_digests_of_v3_unit() noexcept { return digests_of_this_unit(kTest); }

}  // namespace crucible::test
