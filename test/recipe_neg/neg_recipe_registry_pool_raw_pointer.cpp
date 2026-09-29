// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipeRegistry constructor takes the pool as a minted
// fixy::BorrowedRef<RecipePool, Brand>.  A raw RecipePool* skips the
// non-null borrow at the boundary of the registry, so the constructor
// must refuse it.
//
// Expected diagnostic: no constructor takes a RecipePool*.

#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    auto init = ::foundation::effects::testing::init();
    crucible::Arena arena{};
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry registry{&pool, test.alloc};
    return registry.entries().value().empty();
}
