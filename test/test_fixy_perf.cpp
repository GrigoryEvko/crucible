// ── test_fixy_perf — sentinel TU for fixy/Perf.h ──────────────────
//
// FIXY-U-121.  Pulls fixy/Perf.h into a TU compiled under project
// warning flags so the header's static_asserts (sentinel + concept
// resolution + cardinality witness) execute.  Witnesses (8 surfaced
// V1 perf mints — `mint_sense_hub_v2` ships under
// `fixy::perf::v2::` via the sibling sub-umbrella
// `<crucible/fixy/perf/V2.h>` per FIXY-U-122, covered by its own
// sentinel TU `test_fixy_perf_v2.cpp`; the two umbrellas CANNOT
// be co-included in one TU — see Perf.h's top-of-file rationale):
//
//   1. fixy::perf::mint_lock_contention      aliases substrate.
//   2. fixy::perf::mint_pmu_sample           aliases substrate.
//   3. fixy::perf::mint_sched_switch         aliases substrate.
//   4. fixy::perf::mint_sched_tp_btf         aliases substrate.
//   5. fixy::perf::mint_sense_hub            aliases substrate.
//   6. fixy::perf::mint_syscall_latency      aliases substrate.
//   7. fixy::perf::mint_syscall_tp_btf       aliases substrate.
//   8. fixy::perf::mint_workload_profiler    aliases substrate (both
//      3-arg + 4-arg overloads share the same name; identity at the
//      3-arg overload is witnessed below).
//   9. Class-type aliases (LockContention / PmuSample / SchedSwitch /
//      SchedTpBtf / SenseHub / SyscallLatency / SyscallTpBtf /
//      WorkloadProfiler) preserve substrate identity.
//   10. CtxFitsXMint concepts admit ColdInitCtx and reject both
//       BgDrainCtx (wrong row) and HotFgCtx (empty row).
//   11. Cardinality witness — `fixy::perf::` surfaces exactly 8 V1
//       perf mints; the ninth substrate mint (`mint_sense_hub_v2`)
//       ships through the sibling `fixy::perf::v2::` umbrella
//       (FIXY-U-122).
//
// Per CLAUDE.md §XXI the using-decl is name-lookup-only: each fixy::
// re-export must resolve to the SAME substrate function-template
// instantiation (pointer-identity, not behavioural-equivalence).
// `decltype(&fixy::perf::mint_X<...>) == decltype(&perf::mint_X<...>)`
// is the strongest reach witness for free function templates.

#include <crucible/fixy/Perf.h>

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/Senses.h>

#include <type_traits>

namespace fp      = ::crucible::fixy::perf;
namespace perf_   = ::crucible::perf;
namespace eff     = ::crucible::effects;

// ─── 1. Function-template identity — mint_lock_contention ─────────

static_assert(std::is_same_v<
    decltype(&fp::mint_lock_contention<eff::ColdInitCtx>),
    decltype(&perf_::mint_lock_contention<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_lock_contention must be the substrate "
    "function (using-decl preserves crucible::perf:: residency).");

// ─── 2. Function-template identity — mint_pmu_sample ──────────────

static_assert(std::is_same_v<
    decltype(&fp::mint_pmu_sample<eff::ColdInitCtx>),
    decltype(&perf_::mint_pmu_sample<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_pmu_sample must be the substrate "
    "function (using-decl name-lookup-only).");

// ─── 3. Function-template identity — mint_sched_switch ────────────

static_assert(std::is_same_v<
    decltype(&fp::mint_sched_switch<eff::ColdInitCtx>),
    decltype(&perf_::mint_sched_switch<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_sched_switch must be the substrate "
    "function (using-decl name-lookup-only).");

// ─── 4. Function-template identity — mint_sched_tp_btf ────────────

static_assert(std::is_same_v<
    decltype(&fp::mint_sched_tp_btf<eff::ColdInitCtx>),
    decltype(&perf_::mint_sched_tp_btf<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_sched_tp_btf must be the substrate "
    "function (using-decl name-lookup-only).");

// ─── 5. Function-template identity — mint_sense_hub ───────────────

static_assert(std::is_same_v<
    decltype(&fp::mint_sense_hub<eff::ColdInitCtx>),
    decltype(&perf_::mint_sense_hub<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_sense_hub must be the substrate "
    "function (using-decl name-lookup-only).");

// (mint_sense_hub_v2 deferred — see Perf.h top-of-file rationale.)

// ─── 6. Function-template identity — mint_syscall_latency ─────────

static_assert(std::is_same_v<
    decltype(&fp::mint_syscall_latency<eff::ColdInitCtx>),
    decltype(&perf_::mint_syscall_latency<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_syscall_latency must be the substrate "
    "function (using-decl name-lookup-only).");

// ─── 7. Function-template identity — mint_syscall_tp_btf ──────────

static_assert(std::is_same_v<
    decltype(&fp::mint_syscall_tp_btf<eff::ColdInitCtx>),
    decltype(&perf_::mint_syscall_tp_btf<eff::ColdInitCtx>)>,
    "FIXY-U-121: fixy::perf::mint_syscall_tp_btf must be the substrate "
    "function (using-decl name-lookup-only).");

// ─── 8. Function-template identity — mint_workload_profiler ───────
//
// WorkloadProfiler ships TWO overloads (3-arg default-Config / 4-arg
// explicit-Config).  Identity is taken at the 3-arg overload via
// explicit `static_cast<Fn*>` disambiguation; the 4-arg overload
// rides the same using-decl by name so its identity is preserved by
// construction.

using WorkloadProfiler3Arg = ::crucible::perf::WorkloadProfiler(*)(
    const eff::ColdInitCtx&,
    const perf_::Senses*,
    eff::Init) noexcept;
static_assert(std::is_same_v<
    decltype(static_cast<WorkloadProfiler3Arg>(
        &fp::mint_workload_profiler<eff::ColdInitCtx>)),
    decltype(static_cast<WorkloadProfiler3Arg>(
        &perf_::mint_workload_profiler<eff::ColdInitCtx>))>,
    "FIXY-U-121: fixy::perf::mint_workload_profiler (3-arg) must be "
    "the substrate function (using-decl name-lookup-only).");

// ─── 9. Type-alias identity ───────────────────────────────────────

static_assert(std::is_same_v<fp::LockContention, perf_::LockContention>,
    "fixy::perf::LockContention must alias substrate.");

static_assert(std::is_same_v<fp::PmuSample, perf_::PmuSample>,
    "fixy::perf::PmuSample must alias substrate.");

static_assert(std::is_same_v<fp::SchedSwitch, perf_::SchedSwitch>,
    "fixy::perf::SchedSwitch must alias substrate.");

static_assert(std::is_same_v<fp::SchedTpBtf, perf_::SchedTpBtf>,
    "fixy::perf::SchedTpBtf must alias substrate.");

static_assert(std::is_same_v<fp::SenseHub, perf_::SenseHub>,
    "fixy::perf::SenseHub must alias substrate.");

// (SenseHubV2 deferred — see Perf.h top-of-file rationale.)

static_assert(std::is_same_v<fp::SyscallLatency, perf_::SyscallLatency>,
    "fixy::perf::SyscallLatency must alias substrate.");

static_assert(std::is_same_v<fp::SyscallTpBtf, perf_::SyscallTpBtf>,
    "fixy::perf::SyscallTpBtf must alias substrate.");

static_assert(std::is_same_v<fp::WorkloadProfiler, perf_::WorkloadProfiler>,
    "fixy::perf::WorkloadProfiler must alias substrate.");

// ─── 10. Concept-resolution identity ──────────────────────────────
//
// Each CtxFitsXMint concept admits ColdInitCtx and rejects BgDrainCtx
// + HotFgCtx.  The substrate ships these asserts at the mint-
// definition site; we duplicate through the fixy:: layer so a future
// regression that silently relaxed the concept gate (e.g., dropping
// the Init-row requirement) would red THIS TU on top of the
// substrate's own.

static_assert(fp::CtxFitsLockContentionMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsLockContentionMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsLockContentionMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsPmuSampleMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsPmuSampleMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsPmuSampleMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSchedSwitchMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSchedSwitchMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSchedSwitchMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSchedTpBtfMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSchedTpBtfMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSchedTpBtfMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSenseHubMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSenseHubMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSenseHubMint<eff::HotFgCtx>);

// (CtxFitsSenseHubV2Mint deferred — see Perf.h top-of-file rationale.)

static_assert(fp::CtxFitsSyscallLatencyMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSyscallLatencyMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSyscallLatencyMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsSyscallTpBtfMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsSyscallTpBtfMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsSyscallTpBtfMint<eff::HotFgCtx>);

static_assert(fp::CtxFitsWorkloadProfilerMint<eff::ColdInitCtx>);
static_assert(!fp::CtxFitsWorkloadProfilerMint<eff::BgDrainCtx>);
static_assert(!fp::CtxFitsWorkloadProfilerMint<eff::HotFgCtx>);

// ─── 11. Cardinality witness — FLOOR ──────────────────────────────
//
// Per FIXY-U-127 floor-vs-ceiling split (feedback_catalog_cardinality_
// test_drift family): the EXACT ceiling pin (`== 8`) lives in
// fixy/Perf.h colocated with the source-of-truth constant, so a
// contributor incrementing the constant cannot miss the sibling
// assertion at edit time.  THIS TU only holds the FLOOR pin (`>= 8`)
// which catches the inverse direction — an accidental REMOVAL of a
// V1 mint that escaped review.  Growth past 8 is silent here and
// auto-tracked by the header's `==` ceiling.

static_assert(::crucible::fixy::perf::self_test::perf_mint_cardinality >= 8,
    "floor: fixy::perf:: mint cardinality regressed below 8 — a V1 "
    "perf mint was removed without updating both fixy/Perf.h's "
    "colocated ceiling pin AND this floor witness.");

int main() {
    // The substrate's own perf_neg fixtures (if any) exercise the
    // negative requires-clause path; this TU asserts reachability +
    // alias identity + concept resolution.  No runtime call needed —
    // each mint invokes bpf() syscalls which would fail under any
    // unprivileged CI sandbox.  Touch the runtime smoke block so the
    // header's no-throw smoke-test runs under the project's preset
    // semantics.
    ::crucible::fixy::perf::runtime_smoke_test();
    return 0;
}
