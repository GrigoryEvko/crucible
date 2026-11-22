// Negative-compile test. This file must fail to compile.
//
// by_hash(ctx, hash) is a pure projection.  Init is the row of the
// initialization phase, not of a pure lookup, so a context whose row holds
// Init fails before the body of the lookup is considered.
//
// Expected diagnostic: the Subrow constraint of by_hash on the row of the
// context is not satisfied.

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
    const ::fixy::ColdInitCtx cold{eff::testing::init()};
    crucible::RecipePool pool{::fixy::to(arena), init};
    crucible::RecipeRegistry reg{::fixy::to(pool), test.alloc};
    auto wrong = reg.by_hash(cold.in_row<eff::Row<eff::Effect::Init>>(), crucible::RecipeHash{0x1234});
    return wrong.has_value() ? 0 : 1;
}
