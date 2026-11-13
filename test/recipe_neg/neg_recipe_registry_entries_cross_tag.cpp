// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A View tagged source::External must not take the place of the
// source::JsonRegistry view of the registry entries.  The provenance tag
// must stay exact at the boundary where the registry lists its entries.
//
// Expected diagnostic: no conversion from the External view to
// RecipeRegistry::Entries.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

int main() {
    auto test = ::foundation::effects::testing::test();
    auto init = ::foundation::effects::testing::init();
    ::fixy::BumpArena arena{};
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry registry{::fixy::mint_borrowed_ref(pool), test.alloc};

    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(registry.entries().value());
    crucible::RecipeRegistry::Entries registry_entries = external;
    (void)registry_entries;
    return 0;
}
