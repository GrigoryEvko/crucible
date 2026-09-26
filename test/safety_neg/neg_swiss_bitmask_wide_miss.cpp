// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BitMask stores a refined mask whose ceiling is ((1 << kGroupWidth) - 1).
// UINT64_MAX is a wide miss on the AVX2, SSE2 and portable widths.  It
// catches a regression where the mask falls back to a raw uint64_t.

#include <crucible/SwissTable.h>

#include <cstdint>
#include <limits>

// ISA DEPENDENCE.  kGroupWidth is 64 on AVX512BW, 32 on AVX2 and 16 on the
// other arms.  At width 64 the ceiling is UINT64_MAX, so no uint64_t value is
// out of range and the premise of this fixture cannot hold.  A build with
// -march=native on an AVX512BW host would compile the body below clean.  That
// arm fails with its own message instead.
#if defined(__AVX512BW__)
static_assert(crucible::detail::group_width() < 64, "the bitmask ceiling fixture requires a group width below 64");
#else
int main() {
    constexpr crucible::detail::BitMask bad{std::numeric_limits<uint64_t>::max()};
    (void)bad;
}
#endif
