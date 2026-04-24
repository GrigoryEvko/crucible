// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `safe_add_all<T, Xs...>` invoked on a variadic sum
// whose PARTIAL sum overflows the destination integer type.  Per
// #134 the static_assert fires `[Checked_Capacity_Overflow]` at
// the first overflowing step.

#include <crucible/safety/Checked.h>

#include <cstddef>
#include <limits>

using crucible::safety::safe_add_all;

int main() {
    // 3-term sum where the first partial overflows uint32_t:
    //   max(u32)/2 + max(u32)/2 + 100 — the third term overflows.
    constexpr auto half = std::numeric_limits<std::uint32_t>::max() / 2 + 1;
    constexpr auto bad = safe_add_all<std::uint32_t, half, half, 100u>;
    (void)bad;
    return 0;
}
