// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `crucible::safety::ct::eq(a, b)` with byte spans
// of UNEQUAL length from a consteval context.  ct::eq is declared
// `constexpr` and its body opens with
//
//     CRUCIBLE_PRE(a.size() == b.size());
//
// Per fixy-A1-014 (and the macro's documented semantics in
// safety/Pre.h), CRUCIBLE_PRE fires at consteval AND runtime, with
// the consteval branch invoking `__builtin_trap()` — a non-constexpr
// operation that poisons the surrounding consteval call.  When the
// call is wrapped in `static_assert(...)`, the consteval poison
// surfaces as a compile-time error.
//
// Discipline rationale (ConstantTime.h, fixy-A1-014):
//   ct::eq is the LAST line of defense in crypto paths (auth-tag
//   compare, HMAC verify).  Every real crypto use-case has fixed,
//   statically-known lengths — a length mismatch indicates corruption
//   or a refactor error; we trap, not silently return `false`.
//
// HS14 — paired with neg_constanttime_less_signed_type for distinct
// mismatch classes:
//   * Class U (sibling): concept-gate rejection on signed less<>
//   * Class M (THIS file): consteval CRUCIBLE_PRE fire in eq's body
// Together the pair pins both soundness layers of ct::*.
//
// U-141 — Class M fixture (closes ConstantTime slice of #146 A8-P2).

#include <crucible/safety/ConstantTime.h>

#include <cstddef>
#include <span>

namespace {
    // Two compile-time byte buffers with INTENTIONALLY different sizes.
    // The span-only API of ct::eq accepts these as
    // std::span<const std::byte> implicitly via array-to-span CTAD.
    constexpr std::byte buf3[3] = {std::byte{1}, std::byte{2}, std::byte{3}};
    constexpr std::byte buf5[5] = {
        std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}
    };
}

// VIOLATION: ct::eq is constexpr; static_assert forces consteval
// evaluation; CRUCIBLE_PRE inside ct::eq sees `a.size() (3) !=
// b.size() (5)` and invokes __builtin_trap() — non-constexpr, so the
// consteval call is invalid and the static_assert reports a compile
// error.
static_assert(::crucible::safety::ct::eq(
                  std::span<const std::byte>{buf3},
                  std::span<const std::byte>{buf5}),
    "ct::eq with mismatched-length spans must NOT compile; this "
    "fixture exists so a future regression that softens the length "
    "pre into a silent `return false;` is caught at compile time.");

int main() { return 0; }
