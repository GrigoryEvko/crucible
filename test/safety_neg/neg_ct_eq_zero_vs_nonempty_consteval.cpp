// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-014 HS14 fixture #2: ct::eq fires CRUCIBLE_PRE when one
// span is empty and the other is not.
//
// The companion fixture neg_ct_eq_length_mismatch_consteval.cpp covers
// the "two non-empty mismatched sizes" mismatch class.  This fixture
// covers the asymmetric edge: empty vs non-empty.  The legacy
// `(ptr, ptr, n)` triple would have evaluated this as `n == 0 → return
// true` (vacuous truth), regardless of whether either side held real
// bytes.  The span-only signature makes the asymmetry visible at the
// type level — the CRUCIBLE_PRE rejects, not silently returns true.
//
// Why this matters: a caller comparing `auth_tag` of declared length 0
// (e.g. from a botched HMAC init) against a real 16-byte received tag
// would have passed pre-fix.  Post-fix the precondition fires, the
// build refuses, and the bug surfaces at static_assert time.
//
// VIOLATION: consteval `ct::eq(<empty span>, <2-byte span>)` fires
// CRUCIBLE_PRE because the sizes differ (0 ≠ 2).
//
// Expected diagnostic: "non-constant condition for static assertion",
// "__builtin_trap", "contract violation", or equivalent — anything
// proving the consteval invocation was refused.

#include <crucible/safety/ConstantTime.h>

#include <array>
#include <cstddef>
#include <span>

constexpr bool empty_vs_nonempty_eq() {
    constexpr std::array<std::byte, 2> b{
        std::byte{0x42}, std::byte{0x43} };

    // VIOLATION: empty vs 2-byte.  CRUCIBLE_PRE(a.size() == b.size())
    // fires: 0 ≠ 2.
    return crucible::safety::ct::eq(
        std::span<const std::byte>{},
        std::span<const std::byte>{b.data(), b.size()});
}

int main() {
    constexpr bool r = empty_vs_nonempty_eq();
    static_assert(r == false, "this must not compile");
    return 0;
}
