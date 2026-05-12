// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-6 (#1028): Swiss table group width is
// safety::PowerOfTwo<size_t>.  A non-power-of-two width breaks the
// mask/probe arithmetic that relies on one-hot capacity geometry.

#include <crucible/SwissTable.h>

int main() {
  constexpr crucible::detail::GroupWidth bad{std::size_t{48}};
  (void)bad;
}
