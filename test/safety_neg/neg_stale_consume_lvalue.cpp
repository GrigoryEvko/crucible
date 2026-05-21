// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `Stale<T>::consume()` on an LVALUE.  The method
// is REF-QUALIFIED `&&` (rvalue-only) — exactly like Linear<T>'s
// consume — so the compiler rejects an lvalue invocation with an
// overload-resolution error citing the rvalue ref-qualifier.
//
// Discipline rationale (Stale.h:245-249):
//   Stale<T>::consume() &&  exists alongside  Stale<T>::peek() const& .
//   The split encodes the §8 ASGD admission semantic:
//     - .peek()    — non-destructive inspection (admission predicate
//                    reads τ + the value before deciding to admit).
//     - .consume() — destructive extraction (admission accepted; the
//                    value MOVES out of the Stale envelope, leaving
//                    no residual `Stale` instance with a now-stale-
//                    forever payload).
//
//   The `&&` qualifier is the load-bearing discipline.  Without it,
//   callers could `stale_event.consume(); stale_event.consume();` —
//   double-consume against a Stale wrapper that's documented as
//   semantically one-shot.  The compiler enforces the discipline at
//   the call site; the runtime has no defensive check (and shouldn't).
//
//   This mirrors safety::Linear<T> which uses the same `&&`-qualifier
//   discipline for its consume() — both wrappers' value-extraction
//   API is rvalue-only to prevent the moved-from-shell footgun.  The
//   sibling fixture neg_stale_cross_t_mixing covers the
//   type-identity axis (Stale<int> vs Stale<double>); THIS file
//   covers the orthogonal ownership-discipline axis (lvalue vs
//   rvalue).
//
// HS14 — distinct-class fixture pair for Stale:
//   * Class T-cross-T (sibling): cross-payload-type mixing in
//     compose_add — pins the per-T type identity at the wrapper
//     surface.
//   * Class T-rvalue-only (THIS file): consume() lvalue rejection
//     — pins the value-extraction ownership discipline at the
//     method-qualifier level.
//
// FIXY-U-146 — bumps Stale from 1 → 2 fixtures (HS14 floor met).
// Companion to U-146 FinalBy (deleted-copy-of-protected) and U-146
// NotInherited (concept-rejection).

#include <crucible/safety/Stale.h>

namespace {
    // Production-shape Stale consumer.  Real call sites (Cipher /
    // §8 ASGD admission gate) typically hold a Stale<GradientShard>
    // as a local variable, then either peek for diagnostics or
    // consume to extract the inner value after admission.
    using GradientShardStub = int;
}

// Anchor: rvalue consume — moving from a fresh Stale OR an
// explicit std::move(lvalue) IS permitted.  This compiles.
[[maybe_unused]] static GradientShardStub anchor_consume_rvalue(
    ::crucible::safety::Stale<GradientShardStub> event)
{
    return std::move(event).consume();
}

// VIOLATION: Stale<T>::consume() is `&&`-qualified.  Invoking on a
// non-moved lvalue triggers an overload-resolution failure citing
// the rvalue ref-qualifier.  GCC emits "passing 'Stale<int>' as
// 'this' argument discards qualifiers" or "cannot bind rvalue
// reference of type ... to lvalue".
[[maybe_unused]] static GradientShardStub offending_consume_lvalue(
    ::crucible::safety::Stale<GradientShardStub>& event)
{
    return event.consume();   // ERROR: consume() is rvalue-only
}

int main() { return 0; }
