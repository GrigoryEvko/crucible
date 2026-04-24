// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `ensure_bytes_fit<Budget, Used>()` invoked with Used >
// Budget.  Per #134 the static_assert fires `[Byte_Budget_Exceeded]`
// naming the budget-fit discipline — used for arena page sizes,
// cache-line budgets, permission-carrier footprint limits, etc.

#include <crucible/safety/Checked.h>

#include <cstdint>

using crucible::safety::ensure_bytes_fit;
using crucible::safety::safe_struct_bytes;

void compile_time_reject() {
    // A struct with THREE 8-byte fields = 24 bytes raw sum.
    // Budget: single cache line = 64 bytes.  24 fits.
    // But push to 9 fields = 72 bytes → exceeds 64-byte cache line.
    ensure_bytes_fit<64,
        safe_struct_bytes<std::uint64_t, std::uint64_t, std::uint64_t,
                          std::uint64_t, std::uint64_t, std::uint64_t,
                          std::uint64_t, std::uint64_t, std::uint64_t>>();
}

int main() { return 0; }
