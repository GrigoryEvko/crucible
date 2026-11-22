// Negative compile test.  This file must fail to compile.
//
// A Tally counts the checks of a test, and only a test builds one.  The
// foreground context owns no Test capability, so the concept TestContext
// refuses the constructor of the Tally.
//
// Expected diagnostic: the constructor of Tally has no viable candidate, and
// the note names the concept.

#include <fixy/check/Check.h>

#include <utility>

namespace eff = foundation::effects;

int main() {
    auto const ctx = eff::testing::foreground();
    ::fixy::check::Tally checks{ctx, "tally_without_test_context"};
    std::move(checks).close();
    return 0;
}
