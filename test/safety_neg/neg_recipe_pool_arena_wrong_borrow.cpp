// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipePool constructor takes a borrow of the Arena that owns the
// recipes.  A minted borrow of a different type must not take its place,
// so the lifetime dependency stays attached to the real owner.
//
// Expected diagnostic: no constructor takes a BorrowedRef<OtherArena>.

#include <crucible/RecipePool.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>

struct OtherArena {};

int main() {
    OtherArena other{};
    auto init = ::foundation::effects::testing::init();
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(other), init};
    return pool.capacity() == 0;
}
