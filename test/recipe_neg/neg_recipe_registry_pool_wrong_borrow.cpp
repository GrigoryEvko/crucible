// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipeRegistry constructor takes a borrow of the RecipePool that
// owns the recipes.  A minted borrow of the arena must not take its place,
// although the two borrows have the same size.
//
// Expected diagnostic: no constructor takes a BorrowedRef<BumpArena>.

#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{};
    crucible::RecipeRegistry registry{::fixy::mint_borrowed_ref(arena), test.alloc};
    return static_cast<int>(registry.entries().value().size());
}
