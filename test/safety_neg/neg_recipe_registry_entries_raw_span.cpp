// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// RecipeRegistry::entries() gives a span tagged source::JsonRegistry.  A
// raw span must not satisfy a consumer that requires the provenance of
// the registry.
//
// Expected diagnostic: no conversion from a raw span to
// RecipeRegistry::Entries.

#include <crucible/RecipeRegistry.h>

#include <span>

static void consume(crucible::RecipeRegistry::Entries entries) { (void)entries; }

int main() {
    std::span<const crucible::RecipeRegistry::Entry> raw{};
    consume(raw);
    return 0;
}
