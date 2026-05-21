// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing `BoundedMonotonic<T, MaxA>` to a function whose
// signature demands `BoundedMonotonic<T, MaxB>` where MaxA != MaxB.
// The two BoundedMonotonic specializations are UNRELATED types —
// the `auto Max` non-type template parameter is part of the type
// identity, and no implicit conversion bridges instantiations.
//
// Discipline rationale (Mutation.h:522-573):
//   BoundedMonotonic<T, Max> encodes the static upper bound `Max`
//   into the TYPE.  A buffer-index counter typed
//   `BoundedMonotonic<uint32_t, kBufferCapacity>` cannot be passed
//   to an API that demands `BoundedMonotonic<uint32_t, kPlanSlotCap>`
//   because the kBufferCapacity might exceed kPlanSlotCap.  The
//   type system catches this at the call site, not via a runtime
//   bound check at first overrun.
//
//   This is structurally distinct from the existing Class M fixture
//   (neg_pre_macro_bounded_monotonic_bump_at_max):
//   - Sibling Class M: CRUCIBLE_PRE inside bump() fires at consteval
//     when the counter is AT Max — pins the bound-violation guard
//     at the method-body invariant level.
//   - This file Class T: cross-Max template-parameter mismatch at
//     overload resolution — pins the load-bearing role of Max as
//     part of type identity.  A refactor that silently dropped Max
//     from the type signature (e.g. promoting to `BoundedMonotonic
//     <T>` with a default Max) would compile cleanly past the Class
//     M fixture but fail this one.
//
// HS14 — distinct-class fixture pair for BoundedMonotonic:
//   * Class M-bump-past-max (sibling): consteval-pre fires inside
//     bump() — bound-invariant discipline.
//   * Class T-cross-Max (THIS file): typed-overload rejection
//     across distinct Max template parameters — type-identity
//     discipline.
//
// FIXY-U-148 — bumps BoundedMonotonic from 1 → 2 fixtures (HS14
// floor met).  Closes the BoundedMonotonic slice of #146 A8-P2.

#include <crucible/safety/Mutation.h>

#include <cstdint>
#include <utility>

namespace {
    // API entry point demanding a specific Max.  In production this
    // would be a slot-index sink, a refcount cell, or any consumer
    // whose upper bound is a structural property of the slot it
    // owns.
    [[maybe_unused]] void consume_capacity_8(
        ::crucible::safety::BoundedMonotonic<std::uint32_t, 8U>&& /*counter*/)
    {
        // body irrelevant — call-site type-check is the test.
    }
}

// Anchor: same-Max call compiles cleanly — Max=8 matches the
// declared parameter type.
[[maybe_unused]] static void anchor_same_max_call(
    ::crucible::safety::BoundedMonotonic<std::uint32_t, 8U>&& counter)
{
    consume_capacity_8(std::move(counter));
}

// VIOLATION: BoundedMonotonic<uint32_t, 16U> and BoundedMonotonic
// <uint32_t, 8U> are unrelated template instantiations.  The static
// Max=16 cannot be narrowed to Max=8 — that would allow a counter
// with possible value 15 to flow into a slot accepting only 0..8.
// GCC rejects with "cannot convert" / "invalid initialization of
// reference" naming both Max values in the diagnostic chain.
[[maybe_unused]] static void offending_cross_max_call(
    ::crucible::safety::BoundedMonotonic<std::uint32_t, 16U>&& counter)
{
    consume_capacity_8(std::move(counter));   // ERROR: Max=16 ≠ Max=8
}

int main() { return 0; }
