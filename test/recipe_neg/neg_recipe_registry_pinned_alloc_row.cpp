// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name_pinned<T>(ctx, name) does a lookup and a tier admission over
// registry state that does not change.  The projection does not allocate,
// so a context whose row holds Alloc fails the pure-row gate.
//
// Expected diagnostic: the Subrow constraint of by_name_pinned on the row of
// the context is not satisfied.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
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
    auto wrong = reg.by_name_pinned<::fixy::Tolerance::BITEXACT>(runner.in_row<eff::Row<eff::Effect::Alloc>>(),
                                                                 crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
