// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The Swiss table group width is a fixy::PowerOfTwo<size_t>, and the
// checked mint is its only door.  A width that is not a power of two
// breaks the mask and probe arithmetic, which needs a one-hot width.

#include <crucible/SwissTable.h>

#include <fixy/Refined.h>

#include <cstddef>

int main() {
    constexpr crucible::detail::GroupWidth bad = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{48});
    (void)bad;
}
