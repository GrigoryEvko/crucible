// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-ExprPool-4 (#918): ExprPool::IntCacheLiteral is
// Refined<in_range<-128, 127>, int64_t>.  The small-integer cache path
// must reject the value immediately below the closed interval so
// `int_cache_[val - kIntCacheLow]` cannot underflow.

#include <crucible/ir001/ExprPool.h>

int main() {
  constexpr crucible::ExprPool::IntCacheLiteral bad{-129};
  (void)bad;
}
