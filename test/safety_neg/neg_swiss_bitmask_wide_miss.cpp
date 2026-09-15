// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-2 (#1024): BitMask stores a Refined mask whose numeric
// ceiling is ((1 << kGroupWidth) - 1).  UINT64_MAX is a wide miss on the
// default AVX2/SSE/portable widths and catches regressions where the
// Refined carrier silently falls back to a raw uint64_t.

#include <crucible/SwissTable.h>

#include <cstdint>
#include <limits>

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
    constexpr crucible::detail::BitMask bad{std::numeric_limits<uint64_t>::max()};
    (void)bad;
}
#endif
