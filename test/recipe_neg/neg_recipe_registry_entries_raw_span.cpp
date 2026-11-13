// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// RecipeRegistry::entries() gives a View tagged source::JsonRegistry.  The
// raw View inside it must not satisfy a consumer that requires the
// provenance of the registry.
//
// Expected diagnostic: no conversion from a raw View to
// RecipeRegistry::Entries.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

static void consume(crucible::RecipeRegistry::Entries entries) { (void)entries; }

int main() {
    auto test = ::foundation::effects::testing::test();
    auto init = ::foundation::effects::testing::init();
    ::fixy::BumpArena arena{};
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry registry{::fixy::mint_borrowed_ref(pool), test.alloc};
    auto const raw = registry.entries().value();
    consume(raw);
    return 0;
}
