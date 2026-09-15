// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-2 (#1024): BitMask stores a Refined mask whose numeric
// ceiling is ((1 << kGroupWidth) - 1).  This boundary fixture constructs
// exactly one bit above the active SIMD group width, which would make
// lowest()/clear_lowest() report a lane outside the loaded CtrlGroup.

#include <crucible/SwissTable.h>

// ISA DEPENDENCE.  kGroupWidth is 64 on AVX512BW, 32 on AVX2, 16 otherwise
// (SwissTable.h:34-40).  At width 64 the ceiling is UINT64_MAX, so no
// uint64_t value is out of range and this fixture's premise cannot hold.
// Release builds with -march=native, so on an AVX512BW host the body below
// compiles clean.  Fail loudly there instead of silently proving nothing.
#if defined(__AVX512BW__)
static_assert(crucible::detail::group_width() < 64,
              "WRAP-SwissTab-2 fixture requires a group width below 64");
#else
int main() {
    constexpr crucible::detail::BitMask bad{crucible::detail::kGroupMaskCeiling + uint64_t{1}};
    (void)bad;
}
#endif
