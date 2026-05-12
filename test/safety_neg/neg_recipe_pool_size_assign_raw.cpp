// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-2 (#971): RecipePool::size_ is carried as
// RecipePool::Size = safety::Monotonic<uint32_t>.  The only legal
// mutation paths are bump()/advance(); assigning a raw integer would
// bypass the monotonicity gate.
//
// Expected diagnostic: no assignment operator from raw uint32_t.

#include <crucible/RecipePool.h>

int main() {
  crucible::RecipePool::Size count{0u};
  count = 1u;
  return 0;
}
