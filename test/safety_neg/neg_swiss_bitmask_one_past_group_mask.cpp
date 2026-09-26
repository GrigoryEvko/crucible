// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BitMask stores a refined mask whose ceiling is ((1 << kGroupWidth) - 1).
// This boundary fixture constructs the value one above the ceiling.  Such a
// mask makes lowest() and clear_lowest() report a lane outside the loaded
// CtrlGroup.

#include <crucible/SwissTable.h>

// ISA DEPENDENCE.  kGroupWidth is 64 on AVX512BW, 32 on AVX2 and 16 on the
// other arms.  At width 64 the ceiling is UINT64_MAX, so no uint64_t value is
// out of range and the premise of this fixture cannot hold.  A build with
// -march=native on an AVX512BW host would compile the body below clean.  That
// arm fails with its own message instead.
#if defined(__AVX512BW__)
static_assert(crucible::detail::group_width() < 64, "the bitmask ceiling fixture requires a group width below 64");
#else
int main() {
    constexpr crucible::detail::BitMask bad{crucible::detail::kGroupMaskCeiling + uint64_t{1}};
    (void)bad;
}
#endif
