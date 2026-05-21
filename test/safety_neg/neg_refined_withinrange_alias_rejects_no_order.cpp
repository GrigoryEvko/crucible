// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 4 for FIXY-U-161 AlignedTo<N, T> / WithinRange
// <L, H, T> §XVI parameterised-alias closure.
//
// Premise: `safety::WithinRange<L, H, T>` is the named template alias
// for `Refined<in_range<L, H>, T>`.  The underlying predicate
// `in_range<L, H>` has body `x >= L && x <= H` — BOTH operator>= AND
// operator<= are required against T at the deduced types of L and H.
//
// Mismatch class for this fixture (distinct from companions):
//   * THIS fixture (Class C — no ordering at all): a struct with
//     NEITHER operator>= NOR operator<= against int.  Both halves of
//     the in_range body fail substitution.  This is the "totally
//     unordered T" case, mirroring U-159's NoCompareStruct pattern
//     but for the new WithinRange alias surface.
//   * Companion #4 (Class H — half-ordering): a struct with operator<=
//     but NOT operator>= — exactly half the interface in_range needs.
//     Class H is in_range-specific (bounded_above only needs operator<,
//     so OnlyLeqStruct would PASS for MaxBounded but FAIL for
//     WithinRange).  The distinguishing structural feature of
//     in_range vs bounded_above is the dual-operator requirement —
//     Class H proves that distinction matters at the alias level.
//   * Companions #1, #2: AlignedTo (Class P scalar T, Class R reference T).
//
// Substring matched: "no match for" or "PredicateInvocableOn" — the
// in_range body has no if-constexpr-requires guard, so substitution
// failure on operator>=/<= surfaces at constexpr expansion (similar
// to MaxBounded's bounded_above pattern).

#include <crucible/safety/Refined.h>

namespace {

// A struct with NEITHER operator>= NOR operator<= against int.
// in_range<0, 100>(x) evaluates `x >= 0 && x <= 100`; both halves
// fail to find a matching comparison operator.  Distinct from
// companion #4 which has HALF the operators (one but not both).
struct NoOrderStruct {
    int payload = 0;
    // Note: NO operator< overload.
    // Note: NO operator<= overload.
    // Note: NO operator>= overload.
    // Note: NO converting constructor from int.
};

}  // namespace

int main() {
    using crucible::safety::WithinRange;

    // VIOLATION: WithinRange<0, 100, NoOrderStruct> = Refined<
    // in_range<0, 100>, NoOrderStruct>.  in_range's body `x >= 0 &&
    // x <= 100` cannot evaluate — neither operator>= nor operator<=
    // is defined for NoOrderStruct vs int.
    WithinRange<0, 100, NoOrderStruct> bad{NoOrderStruct{}};
    (void)bad;
    // ERROR: no match for 'operator>=' or 'operator<=' (operand types
    // 'NoOrderStruct' and 'int') — predicate body substitution failure.
    return 0;
}
