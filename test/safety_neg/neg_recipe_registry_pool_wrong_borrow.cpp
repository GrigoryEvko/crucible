// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-5 (#980): the registry constructor requires a borrow
// of the actual RecipePool.  BorrowedRef<Arena> must not substitute for
// BorrowedRef<RecipePool>, even though both are pointer-sized.
//
// Expected diagnostic: no constructor from BorrowedRef<Arena>.

#include <crucible/RecipeRegistry.h>

int main() {
  crucible::effects::Test test{};
  crucible::Arena arena{};
  crucible::RecipeRegistry registry{
      crucible::safety::BorrowedRef<crucible::Arena>{arena}, test.alloc};
  return registry.entries().value().empty();
}
