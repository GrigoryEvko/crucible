// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Saturate-4 (#1002): *_sat_into mutates its destination counter.
// A const counter must use *_sat_from instead, preserving the
// read-only/mutating split.

#include <crucible/Saturate.h>

int main() {
  const unsigned counter = 1u;
  auto wrong = crucible::sat::add_sat_into(counter, 2u);
  return static_cast<int>(wrong.value());
}
