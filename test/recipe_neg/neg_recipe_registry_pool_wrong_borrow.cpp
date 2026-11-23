// Negative-compile test. This file must fail to compile.
//
// The RecipeRegistry constructor takes a Ref of the RecipePool that owns
// the recipes.  A Ref of the arena must not take its place, although the
// two Refs have the same size.
//
// Expected diagnostic: no constructor takes a Ref<BumpArena<RecipeArena>>.

#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::RecipeArena> arena =
        ::fixy::mint_bump_arena<crucible::RecipeArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
    crucible::RecipeRegistry registry{::fixy::to(arena), test.alloc};
    return static_cast<int>(registry.entries(::foundation::effects::testing::foreground()).value().size());
}
