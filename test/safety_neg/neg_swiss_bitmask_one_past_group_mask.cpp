// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-2 (#1024): BitMask stores a Refined mask whose numeric
// ceiling is ((1 << kGroupWidth) - 1).  This boundary fixture constructs
// exactly one bit above the active SIMD group width, which would make
// lowest()/clear_lowest() report a lane outside the loaded CtrlGroup.

#include <crucible/SwissTable.h>

int main() {
  constexpr crucible::detail::BitMask bad{
      crucible::detail::kGroupMaskCeiling + uint64_t{1}};
  (void)bad;
}
