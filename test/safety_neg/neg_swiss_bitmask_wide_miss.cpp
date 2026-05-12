// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-SwissTab-2 (#1024): BitMask stores a Refined mask whose numeric
// ceiling is ((1 << kGroupWidth) - 1).  UINT64_MAX is a wide miss on the
// default AVX2/SSE/portable widths and catches regressions where the
// Refined carrier silently falls back to a raw uint64_t.

#include <crucible/SwissTable.h>

#include <cstdint>
#include <limits>

int main() {
  constexpr crucible::detail::BitMask bad{
      std::numeric_limits<uint64_t>::max()};
  (void)bad;
}
