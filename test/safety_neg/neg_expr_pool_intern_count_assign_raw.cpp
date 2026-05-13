// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-ExprPool-2 (#916): ExprPool::intern_count_ is carried as
// ExprPool::InternCount = safety::Monotonic<size_t>.  The legal
// mutation paths are bump()/advance(); assigning a raw size_t would
// bypass the monotonicity contract.
//
// Expected diagnostic: no assignment operator from raw size_t.

#include <crucible/ir001/ExprPool.h>

int main() {
  crucible::ExprPool::InternCount count{std::size_t{0}};
  count = std::size_t{1};
  return 0;
}
