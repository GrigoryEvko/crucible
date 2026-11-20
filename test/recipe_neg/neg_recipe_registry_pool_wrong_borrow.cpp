// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The RecipeRegistry constructor takes a Ref of the RecipePool that owns
// the recipes.  A Ref of the arena must not take its place, although the
// two Refs have the same size.
//
// Expected diagnostic: no constructor takes a Ref<BumpArena>.

#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{};
    crucible::RecipeRegistry registry{::fixy::to(arena), test.alloc};
    return static_cast<int>(registry.entries(::foundation::effects::testing::foreground()).value().size());
}
