// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// positive, mismatch class #2: VALUE-NEGATIVE-SIGNED (wrong-cite-
// to-is_non_zero violator).
//
// Pins the strict-positive lower-bound clause `T{0} < x` against
// a SIGNED witness.  Witness: `positive(int32_t{-1})`.  CRUCIBLE_PRE
// fires `__builtin_trap()` at consteval because positive(-1) correctly
// returns false; the front-end rejects with "non-constant condition".
//
// This fixture exists to catch the canonical "wrong-cite to
// is_non_zero" bug class — the most insidious of the positive-
// predicate failure modes.  The bug shape:
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x != T{0}; }   // BUG
//
// At every UNSIGNED cite (Arena::reserve_block(size_t),
// Arena::alloc_array<T>(size_t), Graph::reduction_body(uint32_t)),
// `is_non_zero(x)` and `positive(x)` agree pointwise (unsigned
// integers cannot be negative).  The companion fixture (zero
// witness) cannot distinguish the two — both correctly reject 0.
//
// But for SIGNED T:
//   * positive(-1)        = -1 > 0  = false   (correct rejection)
//   * is_non_zero(-1)     = -1 != 0 = true    (incorrectly admits!)
//
// So a buggy `positive` that collapsed to `is_non_zero` would:
//
//   1. Pass the zero fixture (rejects 0 correctly).
//   2. Pass every unsigned production cite at runtime.
//   3. Silently admit negative values at any signed cite.
//
// In Crucible production code, signed cites of `decide::positive`
// are rare BUT real: when a signed loop counter, signed delta,
// or post-promotion intermediate flows into a contract that meant
// "strictly above zero" semantically.  A wrong-cite to is_non_zero
// would produce:
//
//   - Loop-decrement underflow: `pre (decide::positive(remaining))`
//     where `remaining` was promoted to signed by an arithmetic
//     subtraction.  Negative-remaining means the loop already
//     under-counted; admitting it produces unbounded iteration or
//     wrong-direction iteration depending on the loop body.
//
//   - Capacity-headroom math: `pre (decide::positive(slack))` where
//     `slack = capacity - used` may go signed-negative if `used`
//     was over-counted; admitting it produces double-write past
//     the capacity bound.
//
//   - Time-budget gating: `pre (decide::positive(remaining_ns))`
//     where the budget computation may go negative if the deadline
//     was already missed; admitting it produces late-firing work
//     that should have been canceled.
//
// The bug is sneaky in code review because:
//
//   1. The two predicate names — `positive` and `is_non_zero` —
//      look like synonyms.  A refactor that renamed the cite
//      (e.g., during a CONTRACT-* migration sweep) is the most
//      likely accidental introduction.  Reviewer sees "non-zero"
//      as innocent, not realizing the strict-positive guarantee
//      was lost.
//
//   2. Unit tests on unsigned cites (Arena, Graph) pass without
//      complaint — the predicates ARE pointwise equal there.
//      The bug only manifests at signed cites, which may have
//      thinner test coverage.
//
//   3. The bug class is structural, not a typo: it's the
//      semantically-wrong predicate, not a misspelling.  Linters
//      and clang-tidy cannot flag it.
//
// Distinct from the companion fixture (positive_zero):
//   * zero (companion)           — witness 0u.  Catches ALWAYS-
//     ACCEPT / OFF-BY-ONE-INCLUSIVE / INVERTED-SENSE.  Cannot
//     catch wrong-cite-to-is_non_zero (which rejects 0 correctly).
//   * negative (this fixture)    — witness -1.  Catches the
//     canonical "is_non_zero confusion" by exploiting that
//     is_non_zero accepts negatives but positive must reject them.
//
// Together the two fixtures span the four named bug classes for
// `positive`.  This is the minimum HS14 needs.
//
// Anti-pattern targeted: wrong-cite (collapses to is_non_zero) /
// strict-vs-non-strict-positive confusion.  Specific shapes:
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x != T{0}; }
//     // WRONG-CITE — admits -1 (and every signed-negative).
//     // Caught: -1 != 0 = true → admits.
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return std::abs(x) > T{0}; }
//     // STRICT-NON-ZERO (semantically same as wrong-cite).
//     // Caught: |-1| > 0 = true → admits.
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x >= T{1}; }
//     // CORRECT for signed (1 ≤ x means x > 0; -1 < 1 → rejects).
//     // This fixture passes — predicate is correct under this
//     // shape.  The fixture only catches WRONG-CITE; correctness-
//     // preserving rephrasings are not pinned (and shouldn't be).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

template <typename T>
[[nodiscard]] constexpr bool gate(T x) noexcept {
    CRUCIBLE_PRE(crucible::decide::positive(x));
    return true;
}

// Negative-one is the minimal signed witness that distinguishes
// strict-positive from is_non_zero.  positive(-1) correctly returns
// false (−1 is not above zero); CRUCIBLE_PRE's __builtin_trap
// fires at consteval.  Catches the wrong-cite-to-is_non_zero bug
// class — the predicate-confusion shape that the companion (zero)
// fixture cannot detect because is_non_zero(0) and positive(0)
// agree.
constexpr auto witness = gate(std::int32_t{-1});

}  // namespace

int main() { return 0; }
