// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipePool constructor takes the arena as a minted
// fixy::BorrowedRef<Arena, Brand>.  A raw Arena* skips the non-null
// borrow at the constructor boundary, so the constructor must refuse it.
//
// Expected diagnostic: no constructor takes an Arena*.

#include <crucible/RecipePool.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::Arena arena{};
    auto init = ::foundation::effects::testing::init();
    crucible::RecipePool pool{&arena, init};
    return pool.capacity() == 0;
}
