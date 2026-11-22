// Negative compile test.  This file must fail to compile.
//
// A Tally that ends with no close would lose its failures.  The constructor
// is nodiscard, so an expression that builds a Tally and drops it at once is
// a warning, and the build makes each warning an error.
//
// Expected diagnostic: the build ignores the value of the nodiscard
// constructor of Tally.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::Tally{test, "tally_dropped_with_no_close"};
    return 0;
}
