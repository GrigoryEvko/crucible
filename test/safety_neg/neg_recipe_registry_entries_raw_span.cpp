// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-3 fixture #1: RecipeRegistry::entries() exposes a
// source::JsonRegistry-tagged span.  A raw span must not implicitly
// satisfy a consumer that requires registry-origin provenance.

#include <crucible/RecipeRegistry.h>

#include <span>

static void consume(crucible::RecipeRegistry::Entries entries) {
  (void)entries;
}

int main() {
  std::span<const crucible::RecipeRegistry::Entry> raw{};
  consume(raw);
  return 0;
}
