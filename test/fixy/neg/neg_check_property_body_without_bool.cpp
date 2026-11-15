// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body of a property gives true when the property holds, so the shrink can
// replay a failure.  A body that gives no bool has no result to shrink, so
// the concept PropertyBody refuses it at the call.
//
// Expected diagnostic: property has no viable candidate, and the note names
// the concept.

#include <fixy/Ctx.h>
#include <fixy/check/Property.h>

namespace eff = foundation::effects;

int main() {
    fixy::TestRunnerCtx const ctx{eff::testing::test()};
    fixy::check::property(ctx, 1, 10, [](fixy::check::Draw& draw) noexcept { static_cast<void>(draw.word()); });
    return 0;
}
