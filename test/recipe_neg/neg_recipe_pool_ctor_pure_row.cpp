// Negative-compile test. This file must fail to compile.
//
// A RecipePool is built in the initialization phase only.  The CallerRow
// of the constructor must contain RecipePool::init_required_row, which is
// Row<Init>.  A pure row cannot build the table.
//
// Expected diagnostic: the Subrow<Row<Init>, Row<>> constraint of the
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
    crucible::RecipePool pool{::fixy::to(arena), init, 32u, std::type_identity<eff::Row<>>{}};
    return pool.capacity() == 0;
}
