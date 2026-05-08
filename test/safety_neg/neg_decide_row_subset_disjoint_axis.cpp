// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// row_subset, mismatch class #2: PAYLOAD AND CTX OF EQUAL
// CARDINALITY BUT DISJOINT ATOMS.
//
// Pins that the predicate correctly rejects a payload row whose
// SIZE matches the Ctx row but whose atoms have ZERO overlap with
// it.  This is the canonical witness against size-only / cardinality
// admission predicates: a buggy implementation comparing only
// `|Payload| <= |Ctx|` would FALSELY ACCEPT `Row<Block> ⊆
// Row<Alloc>` because both have size 1.
//
// Witness `(Payload = Row<Block>, Ctx = Row<Alloc>)`.  Both rows
// have cardinality 1.  Block ∉ {Alloc}, so the predicate must reject
// → row_subset returns false → CRUCIBLE_PRE fires `__builtin_trap()`
// at consteval, which the front-end rejects as "non-constant
// condition".
//
// In production this bug manifests as: a kernel declared
// `Computation<Row<Block>, T>` (may issue a blocking syscall — e.g.
// `read(2)` on a long-NVMe-IO path) being placed into an alloc-
// only-budgeted ExecCtx (e.g. a hot-path Stage that has zero
// blocking budget).  The kernel runs anyway:
//
//   1. The blocking call stalls the hot path; downstream Stages
//      starve their consumers; queue depth spikes.  Augur sees the
//      throughput collapse but the row metadata says "all alloc";
//      drift-attribution has no signal to point at the blocking
//      call as root cause.
//   2. CtxFitsStage's row admission was bypassed by a size-only
//      `pre (row_size_v<Payload> <= row_size_v<Ctx>)` shape: 1 <=
//      1 is true, the predicate falsely admits, the production bug
//      lands.  The companion fixture (extra-effect) would NOT
//      catch this — there |Payload|=2 > |Ctx|=1, size-only would
//      correctly reject.  The two fixtures cover orthogonal
//      buggy-implementation buckets.
//
// The bug is sneaky because:
//
//   1. Both rows are well-formed Met(X) effect rows — no type-
//      system error.
//   2. Cardinalities match, so any naive "are they compatible?"
//      check that compares only the COUNT of atoms passes.
//   3. A buggy predicate using `effects::row_size_v<Payload> <=
//      effects::row_size_v<Ctx>` (CARDINALITY-ONLY) would
//      FALSELY ACCEPT: 1 <= 1 = true.  The fixture pins the
//      MEMBERSHIP requirement.
//   4. A buggy predicate using "any atom in common" overlap test
//      would correctly REJECT (no shared atoms) — but would also
//      WRONGLY REJECT `(Row<Alloc, IO>, Row<Alloc, Block>)` where
//      Alloc IS shared but the IO ∉ {Alloc, Block} mismatch should
//      still cause rejection.  Set membership for EVERY payload
//      atom is the only correct formulation.
//
// Anti-pattern targeted: per-call-site spelling of payload-row
// admission via cardinality / overlap heuristics instead of citing
// this procedure.  Specific shapes:
//
//   pre (effects::row_size_v<Payload> <= effects::row_size_v<Ctx>)
//     // CARDINALITY-ONLY — falsely admits this case (1 <= 1).
//     // Catches NEITHER the disjoint-axis bug NOR partial-overlap
//     // bugs.  Always cite this procedure instead.
//
//   pre (any_overlap_v<Payload, Ctx>)
//     // ANY-OVERLAP — would correctly reject this case (no
//     // overlap) but falsely accepts `(Row<Alloc, IO>, Row<Alloc,
//     // Block>)` (Alloc overlaps but IO doesn't satisfy
//     // membership).  Wrong relation entirely.
//
//   pre (count_shared_atoms_v<Payload, Ctx> > 0)
//     // SAME ANTI-PATTERN as any_overlap; doesn't enforce that
//     // EVERY atom in Payload is in Ctx.
//
// Distinct from the companion fixture (extra-effect):
//   * extra-effect (companion)   — `Row<Alloc, IO> ⊆ Row<Alloc>`.
//     |Payload| > |Ctx|, atoms partially overlap.  Catches the
//     "direction-reversed" and "is_same" bug classes.
//   * disjoint-axis (this fixture) — `Row<Block> ⊆ Row<Alloc>`.
//     |Payload| == |Ctx|, atoms disjoint.  Catches the "size-only"
//     and "any-overlap-passes" bug classes — which the
//     extra-effect fixture would NOT catch (size-only correctly
//     rejects when |Payload| > |Ctx|).
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// that map to non-overlapping buggy implementations.  A correct
// implementation passes both; a buggy implementation that comparing
// only sizes passes (1) and fails (2); a buggy implementation
// reversing direction passes (2) and fails (1); a buggy
// implementation using identity passes both fixtures' negative
// cases but fails the positive static_asserts in test_decide.cpp
// (e.g., `Row<Alloc> ⊆ Row<Alloc, IO>`).
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

using Payload_block_only = fx::Row<fx::Effect::Block>;
using Ctx_alloc_only     = fx::Row<fx::Effect::Alloc>;

// `Payload = {Block}`, `Ctx = {Alloc}` — equal cardinality (1 each)
// but the atoms are disjoint.  row_subset rejects because Block
// is not a member of {Alloc}; CRUCIBLE_PRE's __builtin_trap fires
// at consteval.
constexpr auto witness = gate<Payload_block_only, Ctx_alloc_only>();

}  // namespace

int main() { return 0; }
