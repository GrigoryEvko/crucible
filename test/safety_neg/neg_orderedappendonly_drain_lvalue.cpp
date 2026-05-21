// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `OrderedAppendOnly<T, ...>::drain()` on an
// LVALUE.  The method is REF-QUALIFIED `&&` (rvalue-only) — calling
// on a non-moved lvalue triggers overload-resolution failure citing
// the rvalue ref-qualifier.
//
// Discipline rationale (Mutation.h:326-329):
//   OrderedAppendOnly<T>::drain() && yields the wrapped Storage<T>
//   and leaves *this empty.  The `&&` qualifier encodes the
//   semantic that drain is destructive — the value MOVES out of the
//   wrapper, the wrapper becomes a documented-empty husk, and the
//   caller is the new sole owner.
//
//   Without the rvalue qualifier, callers could
//   `log.drain(); log.drain();` — double-drain against a wrapper
//   that's documented as one-shot.  The compiler enforces the
//   discipline at the call site; the runtime has no defensive
//   check (and shouldn't — that's the type system's job).
//
//   This is structurally distinct from the existing Class M fixture
//   (neg_pre_macro_ordered_appendonly_backward_key):
//   - Sibling Class M: CRUCIBLE_PRE inside append() fires on a
//     backward-key violation — pins the order-preservation
//     invariant at the mutation point.
//   - This file Class T: rvalue-only ref-qualifier rejection at
//     overload resolution time — pins the destructive-extraction
//     ownership discipline.
//
//   Two enforcement layers, two distinct compile errors:
//     (a) order invariant: append's key must not regress.
//     (b) extraction discipline: drain consumes exactly once.
//
// HS14 — distinct-class fixture pair for OrderedAppendOnly:
//   * Class M-backward-key (sibling): consteval-pre fires inside
//     append — order-preservation discipline.
//   * Class T-rvalue-only-drain (THIS file): drain()'s `&&`
//     ref-qualifier rejects lvalue invocation — destructive-
//     extraction ownership discipline.
//
// FIXY-U-149 — bumps OrderedAppendOnly from 1 → 2 fixtures (HS14
// floor met).  Closes the OrderedAppendOnly slice of #146 A8-P2.

#include <crucible/safety/Mutation.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace {
    using LogT = ::crucible::safety::OrderedAppendOnly<std::uint64_t>;
}

// Anchor: rvalue drain compiles cleanly — moving from a freshly
// constructed OrderedAppendOnly is the canonical exit pattern.
[[maybe_unused]] static std::vector<std::uint64_t> anchor_drain_rvalue(
    LogT log)
{
    return std::move(log).drain();
}

// VIOLATION: drain() is `&&`-qualified.  Invoking on a non-moved
// lvalue triggers an overload-resolution failure.  GCC emits
// "passing 'OrderedAppendOnly<...>' as 'this' argument discards
// qualifiers" or "cannot bind rvalue reference".
[[maybe_unused]] static std::vector<std::uint64_t> offending_drain_lvalue(
    LogT& log)
{
    return log.drain();   // ERROR: drain() is rvalue-only
}

int main() { return 0; }
