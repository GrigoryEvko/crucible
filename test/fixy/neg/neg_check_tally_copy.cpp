// Negative compile test.  This file must fail to compile.
//
// A Tally counts the checks of one test.  A copy would count a failure two
// times, or lose the failures of the copy, so the copy constructor is
// deleted with its reason.
//
// Expected diagnostic: a use of the deleted copy constructor of Tally, with
// its reason.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <utility>

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::Tally checks{test, "tally_copy"};
    ::fixy::check::Tally copy{checks};
    std::move(copy).close();
    std::move(checks).close();
    return 0;
}
