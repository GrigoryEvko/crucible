// Negative compile test.  This file must fail to compile.
//
// CRUCIBLE_TEST_ASSERT ends the process through the failure arm of the Check
// family, and only a test calls it.  The foreground context owns no Test
// capability, so the concept TestContext refuses the failure arm at the
// call, also when the condition is true.
//
// Expected diagnostic: fail_assertion has no viable candidate, and the note
// names the concept.

#include <fixy/check/Check.h>

namespace eff = foundation::effects;

int main() {
    auto const ctx = eff::testing::foreground();
    CRUCIBLE_TEST_ASSERT(ctx, 1 + 1 == 2);
    return 0;
}
