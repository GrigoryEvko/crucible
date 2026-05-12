// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-3 fixture #2: a span tagged as source::External
// cannot be substituted for the source::JsonRegistry entries view.
// The trust lane must stay exact at the registry enumeration boundary.

#include <crucible/RecipeRegistry.h>

#include <span>

int main() {
  using Raw = std::span<const crucible::RecipeRegistry::Entry>;
  using ExternalEntries =
      crucible::safety::Tagged<Raw, crucible::safety::source::External>;

  ExternalEntries external{Raw{}};
  crucible::RecipeRegistry::Entries registry_entries = external;
  (void)registry_entries;
  return 0;
}
