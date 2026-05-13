// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-ExprPool-2 (#916): ExprPool::capacity_ is carried as
// ExprPool::Capacity = safety::PowerOfTwo<size_t>.  The Swiss-table
// probe mask is `capacity - 1`, so non-power capacities must be
// rejected at the typed boundary.
//
// Expected diagnostic: Refined<power_of_two, size_t> contract failure.

#include <crucible/ir001/ExprPool.h>

int main() {
  constexpr crucible::ExprPool::Capacity bad{std::size_t{12}};
  (void)bad;
}
