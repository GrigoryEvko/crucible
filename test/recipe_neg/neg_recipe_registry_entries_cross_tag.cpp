// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A span tagged source::External must not take the place of the
// source::JsonRegistry view of the registry entries.  The provenance tag
// must stay exact at the boundary where the registry lists its entries.
//
// Expected diagnostic: no conversion from the External span to
// RecipeRegistry::Entries.

#include <crucible/RecipeRegistry.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <span>

int main() {
    using Raw = std::span<const crucible::RecipeRegistry::Entry>;

    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(Raw{});
    crucible::RecipeRegistry::Entries registry_entries = external;
    (void)registry_entries;
    return 0;
}
