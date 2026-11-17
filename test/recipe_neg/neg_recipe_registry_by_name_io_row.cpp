// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name(ctx, name) is a pure projection over registry state that does not
// change.  A context whose row holds IO fails the Subrow gate on the row of
// the context.
//
// Expected diagnostic: the Subrow constraint of by_name on the row of the
// context is not satisfied.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
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
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), test.alloc};
    auto wrong = reg.by_name(runner.in_row<eff::Row<eff::Effect::IO>>(), crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
