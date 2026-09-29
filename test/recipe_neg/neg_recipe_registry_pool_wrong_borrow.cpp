// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipeRegistry constructor takes a borrow of the RecipePool that
// owns the recipes.  A minted borrow of the Arena must not take its
// place, although the two borrows have the same size.
//
// Expected diagnostic: no constructor takes a BorrowedRef<Arena>.

#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    crucible::Arena arena{};
    crucible::RecipeRegistry registry{::fixy::mint_borrowed_ref(arena), test.alloc};
    return registry.entries().value().empty();
}
