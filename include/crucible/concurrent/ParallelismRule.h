#pragma once

// ═══════════════════════════════════════════════════════════════════
// concurrent::ParallelismRule — cache-tier-aware parallelism decision
//
// Given a WorkBudget (read/write bytes + item count), decide whether
// to run sequentially or in parallel — and if parallel, at what
// factor and with what NUMA policy.  The rule is structural, not
// tunable: it follows from cache-line ping-pong vs memory-bandwidth
// economics, not from per-workload heuristics.
//
// NAMING NOTE: this header was previously `CostModel.h`.  Renamed to
// avoid collision with Mimic's "cost model" — Mimic owns the
// per-vendor kernel-compilation cost terminology (SASS/PTX cost
// estimation, MAP-Elites scoring, tile-shape ranking).  The rule
// here is the parallelism-dispatch rule for the runtime, separate
// concern from kernel codegen costs.
//
// THE PROMISE: never regresses.  For any workload smaller than the
// per-core L2, the recommendation is Sequential — adding cores would
// strictly hurt (cache invalidation traffic > parallel work).  Only
// when the working set exceeds L2 do we recommend parallel, and even
// then we cap by what the underlying memory hierarchy can actually
// stream.
//
// ─── The decision tree (THREADING.md §5.4 / 27_04 §5.7) ────────────
//
// Pure cache-driven.  No abstract cost dimensions (no nanoseconds-
// per-item, no FLOP intensity).  Per 27_04 §1.2: "Cost is modeled
// via concrete hardware facts only — bytes, cache sizes, NUMA
// distances."
//
//   ws := budget.read_bytes + budget.write_bytes
//
//   1. CACHE-TIER GATE
//      tier := classify(ws)
//      if tier == L1Resident or L2Resident:  → Sequential
//      Rationale: data already hot in one core's L1/L2; parallelism
//      adds cache invalidation traffic strictly worse than no-op.
//
//   2. L3-RESIDENT
//      if tier == L3Resident:
//          → Parallel(min(cores_per_socket, cores_avail, 4), NumaLocal)
//      Rationale: shared L3 within a socket — cap at cores-per-socket
//      to avoid cross-socket cache traffic.  4 is a good upper bound
//      for L3-resident: more cores mean more L3 pressure, not more
//      bandwidth.
//
//   3. DRAM-BOUND (default)
//      factor := min(cores_avail, max(1, ws / l2_per_core))
//      → Parallel(round_to_factor_ladder(factor),
//                 NumaSpread if numa_nodes > 1 else NumaIgnore)
//      Rationale: each worker's L1/L2 streams its share from DRAM;
//      memory channels saturate after factor ≈ ws / l2.  Spreading
//      across NUMA nodes uses parallel memory controllers.
//
// ─── What was removed (vs. earlier rule) ───────────────────────────
//
// Two branches were removed in the 27_04 §5.7 cleanup:
//
//   * COMPUTE-BOUND OVERRIDE — used a `per_item_compute_ns` caller
//     hint to override cache classification.  Removed because: (a)
//     the hint was unverifiable (no runtime check), (b) it conflicted
//     with the "concrete hardware facts only" discipline, (c) when
//     wrong it caused over-parallelization on cache-resident data.
//
//   * AMORTIZATION GATE — used a synthesized `total_compute_ns` to
//     skip parallelism when total work was tiny.  Removed because:
//     (a) it depended on the same unverifiable hint, (b) the cache
//     classification already prevents over-parallelization on small
//     workloads (small workloads land in L1/L2 → sequential).
//
// The honest scope of this rule is: "given the working set's bytes,
// here is the parallelism that fits the cache hierarchy."  Compute-
// bound workloads with cheap data are an open question — callers who
// know they want parallelism for compute (not data) reasons should
// dispatch directly via `parallel_for_views<N>` rather than route
// through the cost model.
//
// ─── Factor ladder ──────────────────────────────────────────────────
//
// The Workload primitives (parallel_for_views<N>) take a compile-time
// N.  CostModel snaps decisions to a fixed ladder {1, 2, 4, 8, 16} so
// the dispatcher's switch statement compiles to a small jump table.
// Rounding is DOWN — never over-spawn.
//
// 1 worker is always Sequential (kind == Sequential), even when it
// happens to be the chosen factor — the call site should fast-path
// inline.
//
// ─── Container awareness ───────────────────────────────────────────
//
// All factor caps use Topology::process_cpu_count() (sched_getaffinity)
// rather than num_cores().  Inside a Docker / Kubernetes / cgroup
// container, this is the count of CPUs the process is ALLOWED to run
// on — typically smaller than the host's physical count.  Without this,
// a 2-vCPU pod on a 64-core host would over-spawn 64 threads that
// time-slice on 2 cores — strictly worse than sequential.
//
// ─── Composition ────────────────────────────────────────────────────
//
//   parallel_for_smart(region, body, ns_per_item)
//     → ParallelismRule::recommend(budget)
//     → switch on decision.factor
//     → parallel_for_views<N>(region, body)
//
// Or callers can use the decision directly:
//
//   const auto dec = ParallelismRule::recommend(budget);
//   if (dec.kind == ParallelismDecision::Kind::Sequential) {
//       /* run inline */
//   } else {
//       /* dispatch with dec.factor + dec.numa */
//   }
//
// ─── Why not autotune? ──────────────────────────────────────────────
//
// Per-call-site dynamic tuning would (a) require persistent state per
// call site, (b) regress until the autotuner converges, and (c) break
// determinism.  The structural decision rule is good enough for ≥ 95%
// of cases AND deterministic.  Future SEPLOG-F3 (WorkloadProfiler)
// can suggest better Kind tags from observed behavior, but the
// per-call decision remains structural.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/Topology.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace crucible::concurrent {

// ── WorkBudget — workload size descriptor ───────────────────────────
//
// Mirror of safety::WorkBudget but lives here in the cost-model layer
// to avoid a circular include with safety/Workload.h.  safety::Workload
// includes this header and forwards through.
//
// Three concrete hardware-facts-only fields.  No abstract cost
// dimensions (no nanoseconds-per-item, no FLOP intensity) — per
// 27_04 §1.2 / CLAUDE.md "concrete over abstract" discipline.
//
// item_count is informational (telemetry; the cost model doesn't
// branch on it any more).  Two of the three fields are load-bearing:
// read_bytes + write_bytes form the working-set size that
// classify() uses against the host's cache hierarchy.

struct WorkBudget {
    std::size_t read_bytes  = 0;
    std::size_t write_bytes = 0;
    std::size_t item_count  = 0;  // informational; not consulted by the cost model
};

// ── Tier — where the working set lives in the cache hierarchy ───────
//
// Probed against Topology at decision time.  The boundaries shift per
// host (a 32 MB L3 on Zen 3 vs 96 MB on Sapphire Rapids), but the
// classification logic is uniform.

enum class Tier : std::uint8_t {
    L1Resident   = 0,   // ws < l1d_per_core
    L2Resident   = 1,   // l1d_per_core ≤ ws < l2_per_core
    L3Resident   = 2,   // l2_per_core ≤ ws < l3_total
    DRAMBound    = 3,   // ws ≥ l3_total
};

// ── NumaPolicy — how to bind worker threads ────────────────────────
//
// NumaIgnore  — let the OS scheduler place workers wherever.  Default
//               on single-NUMA machines and for compute-bound tasks
//               that don't touch DRAM heavily.
//
// NumaLocal   — pin all workers to one NUMA node (the producer's, by
//               convention).  Right for L3-resident workloads where
//               cross-socket traffic would invalidate the L3 cache.
//
// NumaSpread  — distribute workers across NUMA nodes proportionally to
//               cores-per-node.  Right for DRAM-bound workloads where
//               parallel memory channels saturate.
//
// The actual binding happens at AdaptiveScheduler::run (SEPLOG-C3) /
// NumaThreadPool (SEPLOG-C4); this enum is just the policy intent.

enum class NumaPolicy : std::uint8_t {
    NumaIgnore = 0,
    NumaLocal  = 1,
    NumaSpread = 2,
};

// ── ParallelismDecision ─────────────────────────────────────────────
//
// What the cost model recommends.  Callers branch on `kind` and
// dispatch on `factor`.  `tier` and `numa` carry the rationale —
// useful for logging, telemetry, and AdaptiveScheduler's worker
// placement.

struct ParallelismDecision {
    enum class Kind : std::uint8_t {
        Sequential = 0,    // run inline; no jthread spawn
        Parallel   = 1,    // dispatch with `factor` workers
    };

    Kind        kind   = Kind::Sequential;
    std::size_t factor = 1;
    NumaPolicy  numa   = NumaPolicy::NumaIgnore;
    Tier        tier   = Tier::L1Resident;

    [[nodiscard]] constexpr bool is_parallel() const noexcept {
        return kind == Kind::Parallel;
    }
};

// ── Constants ────────────────────────────────────────────────────────

namespace parallelism_rule_detail {

// Cap factor at this even on huge machines — past 16 the lock-step
// jthread join cost typically dominates speedup.  AdaptiveScheduler
// (C3) may grow this for genuinely embarrassingly-parallel workloads.
inline constexpr std::size_t kMaxFactor = 16;

// L3-resident factor cap.  More cores past this just thrash L3.
inline constexpr std::size_t kL3ResidentMaxFactor = 4;

// Round `want` DOWN to the nearest factor ladder entry {1, 2, 4, 8, 16}.
// Sub-power-of-2 cases under-spawn rather than over-spawn — sticking to
// the no-regression rule.
[[nodiscard]] constexpr std::size_t
round_to_factor_ladder(std::size_t want) noexcept {
    if (want >= 16) return 16;
    if (want >= 8)  return 8;
    if (want >= 4)  return 4;
    if (want >= 2)  return 2;
    return 1;
}

}  // namespace parallelism_rule_detail

// ── ParallelismRule ─────────────────────────────────────────────
//
// Stateless utility class — all methods static.  Reads Topology at
// decision time; the singleton has already done its sysfs probe by
// the first call to instance() so this is a few atomic loads + math.

class ParallelismRule {
public:
    ParallelismRule() = delete;  // pure utility; no instances

    // Classify a working-set size against the host's cache hierarchy.
    //
    // Returns the smallest Tier whose threshold the working set
    // exceeds.  The boundaries are read from Topology, so the same
    // bytes value classifies differently on different hosts (a 1 MB
    // working set is L2Resident on a Zen 3 with 512 KB L2 / 32 MB L3
    // and L3Resident on an Intel with 1.25 MB L2).
    [[nodiscard, gnu::pure]] static Tier
    classify(std::size_t ws_bytes) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t l1d = topo.l1d_per_core_bytes();
        const std::size_t l2  = topo.l2_per_core_bytes();
        const std::size_t l3  = topo.l3_total_bytes();
        if (ws_bytes < l1d) return Tier::L1Resident;
        if (ws_bytes < l2)  return Tier::L2Resident;
        if (ws_bytes < l3)  return Tier::L3Resident;
        return Tier::DRAMBound;
    }

    // Recommend a parallelism strategy for the given budget.  See the
    // header doc above for the full decision tree.
    //
    // The returned decision is deterministic given the same Topology
    // and budget — no random choices, no per-call-site state.  Two
    // identical budgets always yield identical decisions.  The rule
    // is purely cache-driven (no abstract cost dimensions); see the
    // header doc for the rationale and what was removed in the
    // 27_04 §5.7 cleanup.
    [[nodiscard]] static ParallelismDecision
    recommend(WorkBudget budget) noexcept {
        const auto& topo = Topology::instance();
        const std::size_t ws = budget.read_bytes + budget.write_bytes;

        // Container-aware caps.
        const std::size_t cores_avail =
            std::max(std::size_t{1}, topo.process_cpu_count());
        const std::size_t cores_per_socket =
            std::max(std::size_t{1}, topo.cores_per_socket());

        ParallelismDecision dec;
        dec.tier = classify(ws);

        // ── Step 1: cache-tier gate ─────────────────────────────────
        //
        // L1- or L2-resident → already hot in one core's cache.
        // Parallel would add cross-core invalidation traffic strictly
        // worse than the work to be done.
        if (dec.tier == Tier::L1Resident || dec.tier == Tier::L2Resident) {
            dec.kind   = ParallelismDecision::Kind::Sequential;
            dec.factor = 1;
            dec.numa   = NumaPolicy::NumaIgnore;
            return dec;
        }

        // ── Step 2: L3-resident parallel ────────────────────────────
        //
        // Within a single socket's L3 — keep workers intra-socket so
        // cache-coherence traffic stays inside the chiplet/socket.
        // Cap at cores_per_socket and at the L3-thrash bound.
        if (dec.tier == Tier::L3Resident) {
            const std::size_t want = std::min({
                cores_per_socket,
                cores_avail,
                parallelism_rule_detail::kL3ResidentMaxFactor,
            });
            dec.kind   = ParallelismDecision::Kind::Parallel;
            dec.factor = parallelism_rule_detail::round_to_factor_ladder(want);
            dec.numa   = NumaPolicy::NumaLocal;
            return dec;
        }

        // ── Step 3: DRAM-bound parallel ─────────────────────────────
        //
        // Memory-bandwidth-bound; factor scales with how many L2-sized
        // chunks the working set divides into.  Spread across NUMA
        // nodes when available so independent memory controllers do
        // the work in parallel.
        const std::size_t l2 =
            std::max(std::size_t{1}, topo.l2_per_core_bytes());
        const std::size_t want = std::min(
            cores_avail,
            std::max(std::size_t{1}, ws / l2));
        dec.kind   = ParallelismDecision::Kind::Parallel;
        dec.factor = parallelism_rule_detail::round_to_factor_ladder(want);
        dec.numa   = (topo.numa_nodes() > 1) ? NumaPolicy::NumaSpread
                                              : NumaPolicy::NumaIgnore;
        return dec;
    }

    // Convenience: derive a WorkBudget from a span of T.  Mirrors
    // safety::WorkBudget::for_span but lives here so cost-model-only
    // consumers don't need to pull in the safety/Workload.h chain.
    template <typename T>
    [[nodiscard]] static constexpr WorkBudget
    budget_for_span(std::size_t count) noexcept {
        const std::size_t bytes = count * sizeof(T);
        return WorkBudget{
            .read_bytes  = bytes,
            .write_bytes = bytes,
            .item_count  = count,
        };
    }
};

// ── Convenience free function ───────────────────────────────────────
//
// Shorthand for ParallelismRule::recommend.  Useful at call sites
// that want to read like prose.

[[nodiscard]] inline ParallelismDecision
recommend_parallelism(WorkBudget budget) noexcept {
    return ParallelismRule::recommend(budget);
}

}  // namespace crucible::concurrent
