// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-3 (#972): the constructor requires a borrow of the
// actual Arena owner.  A lookalike object cannot stand in for Arena,
// which keeps the lifetime dependency attached to the real owner type.
//
// Expected diagnostic: no constructor from BorrowedRef<OtherArena>.

#include <crucible/RecipePool.h>

struct OtherArena {};

int main() {
  OtherArena other{};
  auto init = crucible::effects::testing::init();
  crucible::RecipePool pool{
      crucible::safety::BorrowedRef<OtherArena>{other}, init};
  return pool.capacity() == 0;
}
