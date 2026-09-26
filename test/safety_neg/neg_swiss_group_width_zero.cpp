// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The Swiss table group width is a fixy::PowerOfTwo<size_t>, and the
// checked mint is its only door.  Zero is not a power of two.  A zero
// width makes the probe step and the control-byte load invalid.

#include <crucible/SwissTable.h>

#include <fixy/Refined.h>

#include <cstddef>

int main() {
    constexpr crucible::detail::GroupWidth bad = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{0});
    (void)bad;
}
