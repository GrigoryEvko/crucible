// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Saturate-3 (#1001): *_sat_det returns
// DetSafe<Pure, Saturated<T>>.  The DetSafe wrapper must not silently
// decay to the underlying Saturated<T>; consumers that drop the
// determinism pin have to do so explicitly.

#include <crucible/Saturate.h>

int main() {
  crucible::safety::Saturated<unsigned> escaped =
      crucible::sat::add_sat_det<unsigned>(1u, 2u);
  return static_cast<int>(escaped.value());
}
