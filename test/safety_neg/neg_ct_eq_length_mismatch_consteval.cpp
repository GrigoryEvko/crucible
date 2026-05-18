// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-014 HS14 fixture #1: ct::eq fires CRUCIBLE_PRE on length
// mismatch at consteval.
//
// Pre-fix, `ct::eq(const std::byte* a, const std::byte* b, size_t n)`
// took a single length and silently dereferenced `nullptr` when either
// pointer was null — the LAST line of defense in the crypto path was
// the most permissive surface.  Worse: a refactor that fed differently-
// sized buffers (e.g. a 16-byte HMAC tag vs a 17-byte derived buffer)
// produced a silent `false` instead of trapping the bug.
//
// fixy-A1-014 retypes `ct::eq` as `(std::span<const std::byte>,
// std::span<const std::byte>)` with `CRUCIBLE_PRE(a.size() ==
// b.size())`.  Length mismatch is now a CALLER BUG that fires at
// consteval AND runtime via __builtin_trap (consteval) /
// handle_contract_violation (runtime).
//
// VIOLATION: consteval `ct::eq(<3-byte span>, <2-byte span>)` fires
// CRUCIBLE_PRE before the loop body, poisoning the surrounding
// static_assert via __builtin_trap.
//
// Expected diagnostic: "non-constant condition for static assertion",
// "__builtin_trap", "contract violation", or equivalent — anything
// proving the consteval invocation was refused.

#include <crucible/safety/ConstantTime.h>

#include <array>
#include <cstddef>
#include <span>

constexpr bool length_mismatch_eq() {
    constexpr std::array<std::byte, 3> a{
        std::byte{1}, std::byte{2}, std::byte{3} };
    constexpr std::array<std::byte, 2> b{
        std::byte{1}, std::byte{2} };

    // VIOLATION: sizes differ.  Span-only signature can't paper over it
    // the way the legacy (ptr, ptr, n) triple did — CRUCIBLE_PRE fires.
    return crucible::safety::ct::eq(
        std::span<const std::byte>{a.data(), a.size()},
        std::span<const std::byte>{b.data(), b.size()});
}

int main() {
    constexpr bool r = length_mismatch_eq();
    static_assert(r == false, "this must not compile");
    return 0;
}
