// Negative-compile test. This file must fail to compile.
//
// The RecipePool constructor takes the arena as a
// fixy::Ref<BumpArena<RecipeArena>, RecipeArena>, with the brand of the
// arena.  A raw pointer to the arena skips the non-null borrow at the
// constructor boundary, so the constructor must refuse it.
//
// Expected diagnostic: no constructor takes a BumpArena<RecipeArena>*.

#include <crucible/RecipePool.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::BumpArena<crucible::RecipeArena> arena =
        ::fixy::mint_bump_arena<crucible::RecipeArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
    auto init = ::foundation::effects::testing::init();
    crucible::RecipePool pool{&arena, init};
    return pool.capacity() == 0;
}
