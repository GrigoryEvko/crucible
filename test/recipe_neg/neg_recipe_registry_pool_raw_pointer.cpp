// Negative-compile test. This file must fail to compile.
//
// The RecipeRegistry constructor takes the pool as a branded
// fixy::Ref<RecipePool, Brand>.  A raw RecipePool* skips the non-null
// borrow at the boundary of the registry, so the constructor must refuse
// it.
//
// Expected diagnostic: no constructor takes a RecipePool*.

#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    auto init = ::foundation::effects::testing::init();
    ::fixy::BumpArena arena{};
    crucible::RecipePool pool{::fixy::to(arena), init};
    crucible::RecipeRegistry registry{&pool, test.alloc};
    return static_cast<int>(registry.entries(::foundation::effects::testing::foreground()).value().size());
}
