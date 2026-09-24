// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing CallSiteTable::Lineno with INT32_MIN
// (-2147483648) in a constexpr context.
//
// Companion fixture to neg_call_site_lineno_negative.cpp:
//   - That one tests the boundary edge (-1 = 0 - 1, off-by-one).
//   - This one tests the wide miss (INT32_MIN = full negative range —
//     covers "casting an unsigned counter to int32_t and forgetting
//     the sign-bit", or a bit-flipped Python frame lineno from
//     corrupted FFI).
//
// Per WRAP-CallSite-2 (#880), CallSiteTable::Lineno is
// safety::Refined<safety::non_negative, int32_t>.  `Lineno{INT32_MIN}`
// triggers `non_negative(INT32_MIN) == false` → contract violation →
// non-constant expression in constexpr context → ill-formed.
//
// Two fixtures rather than one because HS14 mandates ≥2 negative-
// compile fixtures per new soundness gate, each demonstrating a
// distinct mismatch class.  Different forbidden-value classes
// (boundary edge vs wide miss) cover different drift modes — a future
// regression that admits "any int" silently passes the wide-miss
// fixture, but fails the boundary one if the predicate becomes
// `(x != INT32_MIN)`; vice versa for "drop the lower bound entirely".
// Both fixtures together pin the gate.

#include <crucible/CallSiteTable.h>
#include <climits>

int main() {
    // Wide miss: INT32_MIN exercises the full negative range,
    // catching any predicate that only filters specific magic values
    // rather than the whole non-negative half-line.
    constexpr crucible::CallSiteTable::Lineno bad{int32_t{INT32_MIN}};
    (void)bad;
    return 0;
}
