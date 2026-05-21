// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 of 4 for FIXY-U-161 AlignedTo<N, T> / WithinRange
// <L, H, T> §XVI parameterised-alias closure.
//
// Premise: `WithinRange<L, H, T>` wraps `in_range<L, H>` whose body
// is the DUAL-comparison `x >= L && x <= H`.  This is structurally
// stricter than `MaxBounded<Max, T>`'s `bounded_above<Max>` which
// uses only `x <= Max`.  A struct that has HALF the ordering
// interface — operator<= but no operator>= — compiles cleanly for
// MaxBounded but MUST be rejected by WithinRange.
//
// Mismatch class for this fixture (distinct from companions):
//   * THIS fixture (Class H — half-ordering interface): the struct
//     provides operator<= against int but NOT operator>=.  This
//     fixture is the load-bearing witness that in_range's
//     dual-comparison requirement is structurally meaningful — a
//     half-ordered T satisfies MaxBounded but not WithinRange, and
//     the alias-level rejection is what makes the distinction
//     visible at the construction site rather than at a downstream
//     contract violation.
//   * Companions #1, #2: AlignedTo (Class P scalar, Class R reference).
//   * Companion #3 (Class C — no ordering at all): a struct with
//     neither operator>= nor operator<=.
//
// Production-site relevance: a developer might define a partial
// ordering for a domain type — e.g., a "version" struct that supports
// "is this version at most as new as X" (operator<=) but not "is this
// version at least as old as X" (operator>=).  WithinRange would
// require BOTH halves; the compile error correctly surfaces the
// half-ordering misuse.
//
// Substring matched: "no match for 'operator>='" — the operator<=
// half is satisfied but operator>= is missing, so the failure pins
// to the specific missing-half-of-interface diagnostic.

#include <crucible/safety/Refined.h>

namespace {

// A struct with operator<= (against int) but NOT operator>=.
// in_range<0, 100>(x) evaluates `x >= 0 && x <= 100`.  The right half
// succeeds (operator<= IS defined); the LEFT half (`x >= 0`) fails
// substitution because no operator>= exists.  This fires AT the
// constexpr expansion of the lambda body, NOT at a coarser
// "is T ordered" trait — which is precisely the structural rigor
// the §XVI alias discipline demands.
struct OnlyLeqStruct {
    int payload = 0;
    constexpr bool operator<=(int rhs) const noexcept { return payload <= rhs; }
    // Note: NO operator>= overload — exactly half the interface
    // in_range needs.
};

}  // namespace

int main() {
    using crucible::safety::WithinRange;

    // VIOLATION: WithinRange<0, 100, OnlyLeqStruct>{...}.  The
    // in_range<0, 100> body evaluates `x >= 0 && x <= 100` —
    // operator<= matches but operator>= does not.  Compile error
    // pins to the missing operator>= candidate.
    WithinRange<0, 100, OnlyLeqStruct> bad{OnlyLeqStruct{}};
    (void)bad;
    // ERROR: no match for 'operator>=' (operand types 'OnlyLeqStruct'
    // and 'int') — half-ordering interface insufficient for in_range's
    // dual-comparison body.
    return 0;
}
