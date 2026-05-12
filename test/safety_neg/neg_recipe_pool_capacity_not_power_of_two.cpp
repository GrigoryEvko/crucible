// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-2 (#971): RecipePool::capacity_ is carried as
// RecipePool::Capacity = safety::PowerOfTwo<uint32_t>.  Swiss-table
// probing relies on mask arithmetic (`capacity - 1`), so a non-power
// capacity cannot be admitted at the type boundary.
//
// Expected diagnostic: Refined<power_of_two, uint32_t> contract failure.

#include <crucible/RecipePool.h>

int main() {
  constexpr crucible::RecipePool::Capacity bad{12u};
  (void)bad;
}
