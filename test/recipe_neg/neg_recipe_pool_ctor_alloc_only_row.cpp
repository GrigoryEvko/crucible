// Negative-compile test. This file must fail to compile.
//
// The RecipePool constructor allocates its table, but Alloc alone is not
// the initialization phase.  The CallerRow must contain Init, and not only
// the allocation capability.
//
// Expected diagnostic: the Subrow<Row<Init>, Row<Alloc>> constraint of the
// constructor is not satisfied.

#include <crucible/RecipePool.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace eff = ::foundation::effects;

int main() {
    auto arena = ::fixy::mint_bump_arena<crucible::RecipeArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{1 << 20});
    auto init = eff::testing::init();
    crucible::RecipePool pool{::fixy::to(arena), init, 32u, std::type_identity<eff::Row<eff::Effect::Alloc>>{}};
    return pool.capacity() == 0;
}
