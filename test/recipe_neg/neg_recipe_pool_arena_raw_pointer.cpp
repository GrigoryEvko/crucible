// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipePool constructor takes the arena as a minted
// fixy::BorrowedRef<BumpArena, Brand>.  A raw pointer to the arena skips the
// non-null borrow at the constructor boundary, so the constructor must
// refuse it.
//
// Expected diagnostic: no constructor takes a BumpArena*.

#include <crucible/RecipePool.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::BumpArena arena{};
    auto init = ::foundation::effects::testing::init();
    crucible::RecipePool pool{&arena, init};
    return pool.capacity() == 0;
}
