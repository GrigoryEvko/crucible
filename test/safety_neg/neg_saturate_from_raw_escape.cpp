// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Saturate-4 (#1002): *_sat_from returns Saturated<T> so the
// clamped bit is carried out of memory-resident counter arithmetic.
// It must not implicitly decay to raw T.

#include <crucible/Saturate.h>

int main() {
  unsigned counter = ~0u;
  unsigned wrong = crucible::sat::add_sat_from(counter, 1u);
  return static_cast<int>(wrong);
}
