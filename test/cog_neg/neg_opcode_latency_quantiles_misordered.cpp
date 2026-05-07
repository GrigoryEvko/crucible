// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for cog::OrderedLatencyQuantiles (GAPS-187 #1212).
//
// Premise: cog::OrderedLatencyQuantiles is typed as
//   safety::Refined<quantile_ordered, cog::LatencyQuantiles>
// where `quantile_ordered` is a constexpr lambda that returns
// `q.p50_ns <= q.p99_ns && q.p99_ns <= q.p999_ns`.  The Refined ctor
// pre-clause refuses values that violate the invariant — under
// semantic=enforce the runtime abort fires immediately; in
// manifestly-constant-evaluated context (the static_assert below) the
// contract violation makes the call non-constant per P1494R5, and the
// static_assert is ill-formed.
//
// Why this is the load-bearing soundness gate:
//
// Histogram quantile ordering p50 ≤ p99 ≤ p999 is a STRUCTURAL
// invariant of any well-formed measurement: by definition the value
// at the 50th percentile cannot exceed the value at the 99th
// percentile, which cannot exceed the value at the 99.9th percentile.
// A triple that violates the invariant is one of:
//
//   1. Disk-corrupted (Cipher cold-tier load read garbage bytes);
//   2. Federation-imported under a different histogram convention
//      (peer org reports descending percentiles, our convention is
//      ascending);
//   3. Calibrator silently mislabeled the fields (e.g. swapped
//      p50_ns ↔ p999_ns at write time);
//   4. Hostile preset writer fabricated values to bypass throughput
//      gating (set p50 huge so the partition optimiser thinks the
//      Cog is slower than reality and avoids placing kernels here).
//
// In all four cases the downstream consumer (Mimic MAP-Elites
// pruning, partition optimiser cost-surface, FitsCog throughput
// envelope) would silently propagate the corruption into kernel
// scheduling decisions.  The Refined gate at construction stops the
// corruption at the boundary where it enters the type system.
//
// Companion fixture: neg_opcode_latency_rejects_psu_rail.cpp
//   * That one tests rejection at the CONCEPT gate — HasOpcodeTable<K>
//     refuses non-substrate CogKind atoms (PsuRail / BmcSensor /
//     aggregates).  Distinct mismatch class (concept substitution
//     failure).
//   * This one tests rejection at the DATA-INVARIANT gate — the pre
//     clause on OrderedLatencyQuantiles refuses non-monotone triples
//     at construction.  Distinct mismatch class (precondition
//     contract violation on a Refined wrapper).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "static assertion failed" / "contract" /
// "precondition" / "quantile_ordered" / "non-constant" / "Refined" /
// "OrderedLatencyQuantiles" / "GAPS-187" pointing at the static_assert
// call site below.

#include <crucible/cog/OpcodeLatencyTable.h>

namespace cog = crucible::cog;

// Constructing OrderedLatencyQuantiles with a triple where p50 > p99
// (here: p50=99, p99=50, p999=30) violates the `quantile_ordered`
// predicate inside the Refined ctor's pre-clause.  In manifestly-
// constant-evaluated context (the static_assert below), the contract
// violation makes the call not a constant expression, so the
// static_assert is ill-formed — the build fails here.
constexpr cog::OrderedLatencyQuantiles BAD_QUANTILES_FIXTURE{
    cog::LatencyQuantiles{
        std::uint32_t{99},   // p50
        std::uint32_t{50},   // p99  — violates p50 ≤ p99
        std::uint32_t{30}}}; // p999 — violates p99 ≤ p999

static_assert(BAD_QUANTILES_FIXTURE.peek().p50_ns == 99,
    "GAPS-187: cog::OrderedLatencyQuantiles MUST refuse non-monotone "
    "quantile triples at the Refined precondition contract.  If this "
    "static_assert ever evaluates successfully, a hostile preset "
    "writer / disk-corrupted snapshot / federation-imported triple "
    "with reversed-percentile convention would slip through field "
    "construction and corrupt every downstream scheduling decision "
    "that compares throughput envelopes across Cogs.");

int main() { return 0; }
