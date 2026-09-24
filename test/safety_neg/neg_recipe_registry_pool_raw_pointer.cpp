// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-5 (#980): RecipeRegistry seeds from a
// RecipeRegistry::PoolBorrow = safety::BorrowedRef<RecipePool>.  A raw
// RecipePool* would erase the explicit non-null pool borrow at the
// registry boundary.
//
// Expected diagnostic: no constructor from RecipePool*.

#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = crucible::effects::testing::test();
    auto init = ::foundation::effects::testing::init();
    crucible::Arena arena{};
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry registry{&pool, test.alloc};
    return registry.entries().value().empty();
}
