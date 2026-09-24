// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipePool constructor allocates its table, but Alloc alone is not
// the initialization phase.  The CallerRow must contain Init, and not only
// the allocation capability.
//
// Expected diagnostic: the Subrow<Row<Init>, Row<Alloc>> constraint of the
// constructor is not satisfied.

#include <crucible/RecipePool.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace eff = ::foundation::effects;

int main() {
    crucible::Arena arena{};
    auto init = eff::testing::init();
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init, 32u,
                              std::type_identity<eff::Row<eff::Effect::Alloc>>{}};
    return pool.capacity() == 0;
}
