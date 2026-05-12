// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-3 (#972): RecipePool's arena dependency is admitted
// as RecipePool::ArenaBorrow = safety::BorrowedRef<Arena>.  Passing a
// raw Arena* would erase the non-null borrowed-reference gate at the
// constructor boundary.
//
// Expected diagnostic: no constructor from Arena*.

#include <crucible/RecipePool.h>

int main() {
  crucible::Arena arena{};
  crucible::effects::Init init{};
  crucible::RecipePool pool{&arena, init};
  return pool.capacity() == 0;
}
