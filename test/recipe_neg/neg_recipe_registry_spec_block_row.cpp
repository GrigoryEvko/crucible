// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name_spec(ctx, name) is the two-axis RecipeSpec projection.  It only
// reads registry state, so a context whose row can block fails at
// substitution.
//
// Expected diagnostic: the Subrow constraint of by_name_spec on the row of
// the context is not satisfied.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    ::fixy::BumpArena arena{};
    auto init = eff::testing::init();
    auto test = eff::testing::test();
    const ::fixy::TestRunnerCtx runner{eff::testing::test()};
    crucible::RecipePool pool{::fixy::to(arena), init};
    crucible::RecipeRegistry reg{::fixy::to(pool), test.alloc};
    auto wrong = reg.by_name_spec(runner.in_row<eff::Row<eff::Effect::Block>>(), crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
