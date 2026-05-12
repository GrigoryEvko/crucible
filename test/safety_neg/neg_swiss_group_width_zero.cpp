// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-6 (#1028): Swiss table group width is
// safety::PowerOfTwo<size_t>.  Zero is not a power of two and would
// make probe advancement and control-byte loads structurally invalid.

#include <crucible/SwissTable.h>

int main() {
  constexpr crucible::detail::GroupWidth bad{std::size_t{0}};
  (void)bad;
}
