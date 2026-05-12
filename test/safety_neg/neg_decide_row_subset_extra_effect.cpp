// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// row_subset, mismatch class #1: PAYLOAD CARRIES AN EXTRA EFFECT
// NOT DECLARED IN CTX.
//
// Pins that the predicate correctly rejects a payload row that
// strictly contains the Ctx row PLUS an extra atom — the candidate
// would emit an effect the consuming context cannot absorb.
//
// Witness `(Payload = Row<Alloc, IO>, Ctx = Row<Alloc>)`.  Payload
// declares it may emit BOTH Alloc and IO effects; Ctx admits only
// Alloc.  IO is in payload but not in Ctx → row_subset returns
// false → CRUCIBLE_PRE fires `__builtin_trap()` at consteval, which
// the front-end rejects as "non-constant condition".
//
// In production this bug manifests as: a kernel declared
// `Computation<Row<Alloc, IO>, T>` (intends to allocate AND perform
// I/O) being placed into a Stage / Pipeline / ExecCtx whose row is
// only `Row<Alloc>` (alloc-only).  The kernel runs anyway — but
// the I/O calls happen inside an alloc-only-budgeted scope:
//
//   1. runtime observer's drift-attribution sees an I/O latency spike but the
//      ExecCtx declared no I/O budget; mis-classifies as
//      "unexpected blocking" and fires the wrong recommendation
//      (e.g. parallelism downgrade) rather than the right one
//      (extend the Ctx row, or quarantine the kernel).
//   2. CtxFitsStage's payload-row admission gate that should have
//      rejected this at compile time was bypassed because the call
//      site spelled `pre (effects::row_size_v<Payload> <=
//      effects::row_size_v<Ctx>)` (the size-only anti-pattern) —
//      catastrophic since |Payload|=2, |Ctx|=1 so the size check
//      DOES fire here, but on `(Row<IO>, Row<Alloc>)` (same
//      cardinality, disjoint atoms, see companion fixture) the
//      size-only check would pass.
//
// The bug is sneaky because:
//
//   1. Both rows use atoms from the same Effect enum universe; no
//      type-system error catches it.
//   2. The test runs FINE on the developer's box because the
//      I/O call happens to fit within the ExecCtx's wall-clock
//      budget AT THAT MOMENT.  Production load surfaces it.
//   3. A buggy predicate using `effects::is_subrow_v<Ctx, Payload>`
//      (DIRECTION REVERSED) would FALSELY ACCEPT this case:
//      `{Alloc} ⊆ {Alloc, IO}` is true.  The fixture pins the
//      direction.
//   4. A buggy predicate using `std::is_same_v<Payload, Ctx>`
//      (STRUCTURAL EQUALITY) would correctly reject — but would
//      ALSO refuse every legal upgrade `(Row<Alloc>, Row<Alloc>)`
//      = identity which the positive static_asserts in
//      test_decide.cpp catch.
//
// Anti-pattern targeted: per-call-site spelling of payload-row
// admission instead of citing this procedure.  Specific shapes:
//
//   pre (effects::row_size_v<Payload> <= effects::row_size_v<Ctx>)
//     // CARDINALITY-ONLY — works for this case (2 > 1) but fails
//     // on disjoint same-size rows (see companion fixture).
//
//   pre (effects::is_subrow_v<Ctx, Payload>)
//     // DIRECTION REVERSED — falsely accepts every legal Payload
//     // upgrade as "valid" admission.  Off-by-direction is fatal.
//
//   pre (effects::row_size_v<Payload> == effects::row_size_v<Ctx>)
//     // EXACT-CARDINALITY — refuses every legal proper-subset
//     // upgrade.  Almost certainly a typo of `<=`.
//
// Distinct from the companion fixture (disjoint-axis):
//   * extra-effect (this fixture) — `Row<Alloc, IO> ⊆ Row<Alloc>`.
//     |Payload| > |Ctx|, atoms partially overlap.  Catches the
//     "direction-reversed" and "is_same" bug classes.
//   * disjoint-axis (companion)   — `Row<Block> ⊆ Row<Alloc>`.
//     |Payload| == |Ctx|, atoms disjoint.  Catches the "size-only"
//     and "any-overlap-passes" bug classes — which the
//     extra-effect fixture would NOT catch (size-only correctly
//     rejects when |Payload| > |Ctx|).
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// that map to non-overlapping buggy implementations.  A reviewer
// satisfying one fixture must verify the other still holds.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

namespace {

namespace fx = crucible::effects;

template <typename Payload, typename Ctx>
[[nodiscard]] constexpr bool gate() noexcept {
    CRUCIBLE_PRE((crucible::decide::row_subset<Payload, Ctx>()));
    return true;
}

using Payload_alloc_io = fx::Row<fx::Effect::Alloc, fx::Effect::IO>;
using Ctx_alloc_only   = fx::Row<fx::Effect::Alloc>;

// `Payload = {Alloc, IO}`, `Ctx = {Alloc}` — payload has an extra
// IO atom not declared in Ctx.  row_subset rejects; CRUCIBLE_PRE's
// __builtin_trap fires at consteval.
constexpr auto witness = gate<Payload_alloc_io, Ctx_alloc_only>();

}  // namespace

int main() { return 0; }
