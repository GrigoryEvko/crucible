// Negative-compile test. This file must fail to compile.
//
// entries(ctx) gives only a read-only span over entries that the
// constructor filled.  A context whose row holds Bg is not pure, so this
// projection refuses it.
//
// Expected diagnostic: the Subrow constraint of entries on the row of the
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
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    crucible::RecipePool pool{::fixy::to(arena), init};
    crucible::RecipeRegistry reg{::fixy::to(pool), test.alloc};
    auto wrong = reg.entries(bg.in_row<eff::Row<eff::Effect::Bg>>());
    return wrong.value().size() == 0 ? 0 : 1;
}
