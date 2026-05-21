// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `AppendOnly<T, ...>::drain()` on an LVALUE.
// The method is REF-QUALIFIED `&&` (rvalue-only, Mutation.h:213) —
// calling on a non-moved lvalue triggers overload-resolution failure
// citing the rvalue ref-qualifier.
//
// Discipline rationale (Mutation.h:211-215):
//   AppendOnly<T>::drain() && yields the wrapped Storage<T> and
//   leaves *this empty (Graded::consume() forwarding).  The `&&`
//   qualifier encodes the semantic that drain is destructive — the
//   container MOVES out of the wrapper, the wrapper becomes a
//   documented-empty husk, and the caller is the new sole owner.
//
//   Without the rvalue qualifier, callers could
//   `log.drain(); log.drain();` — double-drain against a wrapper
//   that's documented as one-shot.  The compiler enforces the
//   discipline at the call site; the runtime has no defensive
//   check (and shouldn't — that's the type system's job).
//
//   This is structurally distinct from the existing Class T-
//   redundancy-static-assert fixtures (neg_appendonly_over_writeonce
//   + neg_appendonly_over_writeoncenonnull):
//   - Sibling Class T-redundancy: static_assert inside the class
//     body fires when AppendOnly is instantiated over WriteOnce<T>
//     or WriteOnceNonNull<T*> — pins the wrapper-composition rule
//     at class-instantiation time.
//   - This file Class T-rvalue-only: drain()'s `&&` ref-qualifier
//     rejects lvalue invocation at OVERLOAD-RESOLUTION time — pins
//     the destructive-extraction ownership discipline.
//
//   Two enforcement layers, two distinct compile errors:
//     (a) composition redundancy: nested-WriteOnce<T> is structurally
//         subsumed by AppendOnly's own immutability promise.
//     (b) extraction discipline: drain consumes exactly once.
//
// HS14 — distinct-class fixture coverage for AppendOnly<T>:
//   * Class T-redundancy-WriteOnce (sibling
//     neg_appendonly_over_writeonce): static_assert on
//     AppendOnly<WriteOnce<T>> fires at instantiation.
//   * Class T-redundancy-WriteOnceNonNull (sibling
//     neg_appendonly_over_writeoncenonnull): same mechanism, the
//     pointer-specialization variant.
//   * Class T-rvalue-only-drain (THIS file): drain()'s `&&`
//     ref-qualifier rejects lvalue invocation at overload
//     resolution — different sub-class of Class T (method-level
//     ref-qualifier vs class-level static_assert).
//
//   Mirrors the OrderedAppendOnly pair pattern already shipped
//   under fixy-U-149 (Class M-backward-key + Class T-rvalue-only-
//   drain).
//
// FIXY-U-152 — closes the AppendOnly slice of #146 A8-P2.  Existing
// coverage of the wrapper was entirely composition-rule-side
// (the two redundancy fixtures); the destructive-extraction
// discipline at the drain() boundary had no fixture witness.

#include <crucible/safety/Mutation.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace {
    using LogT = ::crucible::safety::AppendOnly<std::uint64_t>;
}

// Anchor: rvalue drain compiles cleanly — moving from a freshly
// constructed AppendOnly is the canonical exit pattern.
[[maybe_unused]] static std::vector<std::uint64_t> anchor_drain_rvalue(
    LogT log)
{
    return std::move(log).drain();
}

// VIOLATION: drain() is `&&`-qualified.  Invoking on a non-moved
// lvalue triggers an overload-resolution failure.  GCC emits
// "passing 'AppendOnly<...>' as 'this' argument discards
// qualifiers" or "cannot bind rvalue reference".
[[maybe_unused]] static std::vector<std::uint64_t> offending_drain_lvalue(
    LogT& log)
{
    return log.drain();   // ERROR: drain() is rvalue-only
}

int main() { return 0; }
