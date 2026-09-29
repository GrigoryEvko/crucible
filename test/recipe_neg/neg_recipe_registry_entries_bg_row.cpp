// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// entries<CallerRow>() gives only a read-only span over entries that the
// constructor filled.  A Bg row is not pure, so this projection must
// refuse it.
//
// Expected diagnostic: the Subrow<Row<Bg>, Row<>> constraint of entries
// is not satisfied.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    crucible::Arena arena{};
    auto init = eff::testing::init();
    auto test = eff::testing::test();
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), test.alloc};
    auto wrong = reg.entries<eff::Row<eff::Effect::Bg>>();
    return wrong.value().empty() ? 0 : 1;
}
