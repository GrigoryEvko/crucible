// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipePool constructor takes a Ref of the BumpArena that owns the
// recipes.  A Ref of a different type must not take its place, so the
// lifetime dependency stays attached to the real owner.
//
// Expected diagnostic: no constructor takes a Ref<OtherArena>.

#include <crucible/RecipePool.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

struct OtherArena {};

int main() {
    OtherArena other{};
    auto init = ::foundation::effects::testing::init();
    crucible::RecipePool pool{::fixy::to(other), init};
    return pool.capacity() == 0;
}
