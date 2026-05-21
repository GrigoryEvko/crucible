#pragma once

// ── crucible::fixy::perf — perf-tree mints under fixy:: ───────────
//
// FIXY-U-121.  Re-exports EIGHT of the nine perf-tree §XXI mint
// factories under `fixy::perf::` so callers who include only the
// fixy umbrella do not have to descend into the perf/ tree to mint
// cold-init observation hubs.
//
// The V2-hub mint factory is DELIBERATELY OMITTED.  SenseHub v1 and
// v2 were authored as alternative-build implementations sharing
// `crucible::perf::` namespace identifiers (`Idx`, `NUM_COUNTERS`,
// originally `CoverageReport` — renamed to `LoadReport` per
// FIXY-U-121a).  Co-including both headers in one TU surfaces three
// ODR / unscoped-vs-scoped enum / different-enumerator-value
// collisions; the CMake toggle `CRUCIBLE_SENSE_HUB_V2` confirms the
// design intent (opt-in replacement, not co-shipped second hub).
// `fixy::perf::v2::` will surface the V2 mint once the substrate
// resolves the V2/V1 namespace split; until then, callers who need
// V2 include `<crucible/perf/SenseHubV2.h>` directly.
//
// Per CLAUDE.md §XXI Universal Mint Pattern, each re-export preserves
// the substrate's CtxFitsXMint concept gate (Init-row admission), the
// `[[nodiscard]] inline ... noexcept` qualifiers (constexpr is NOT
// preserved — these mints invoke BPF setup which is not consteval-
// evaluable; the inventory rightly shows `-` for the constexpr
// column), and the ctx-bound authorization shape (`Ctx const&` first
// parameter + `effects::Init init` cap-tag for the BPF-load proof).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   perf::mint_lock_contention(ctx, init)        — LockContention.h
//   perf::mint_pmu_sample(ctx, init)             — PmuSample.h
//   perf::mint_sched_switch(ctx, init)           — SchedSwitch.h
//   perf::mint_sched_tp_btf(ctx, init)           — SchedTpBtf.h
//   perf::mint_sense_hub(ctx, init)              — SenseHub.h
//   perf::mint_syscall_latency(ctx, init)        — SyscallLatency.h
//   perf::mint_syscall_tp_btf(ctx, init)         — SyscallTpBtf.h
//   perf::mint_workload_profiler(ctx, senses, init[, cfg])
//                                                — WorkloadProfiler.h
//
// DEFERRED (alternative-build header — see top-of-file rationale):
//   the V2-hub mint factory in SenseHubV2.h
//
// ── Why every mint is Init-row gated ──────────────────────────────
//
// Every perf hub loads an eBPF program via `bpf(BPF_PROG_LOAD, ...)` +
// attaches via `perf_event_open` / `tracepoint__attach_btf` / etc.
// Both are kernel-mediated state mutations belonging to the startup-
// only Init row.  Hot foreground and background-drain contexts must
// not stand up fresh observation programs (they read from EXISTING
// hubs minted at Init time by the Keeper or bench harness).
//
// All eight surfaced concepts statically reject `HotFgCtx` and
// `BgDrainCtx`; the substrate ships those rejection static_asserts
// at the mint definition site, this header relays them through
// `fixy::perf::` so the same negative reach holds through the
// umbrella.
//
// ── Return-type discipline ────────────────────────────────────────
//
// Seven of eight surfaced mints return `std::optional<X>` — the
// `X::load(init)` path can fail (BPF program rejected by verifier,
// kernel feature missing, CAP_BPF absent under a hardened seccomp
// profile, etc.).  `mint_workload_profiler` returns `WorkloadProfiler`
// directly via the in-place constructor — its dependency on the
// `Senses*` pointer means construction is unconditionally feasible
// given a valid Senses surface (the substrate ctor handles degraded-
// fleet paths internally).  This shape difference is preserved
// through the using-decl; downstream callers see the same return-
// type contract.
//
// ── Allocation discipline ─────────────────────────────────────────
//
// None of these mints are `constexpr` — `X::load(init)` invokes
// kernel syscalls.  Per §XXI, the discipline still requires
// `[[nodiscard]]` + `noexcept` + ctx-bound first param + concept-
// gate single requires-clause, and all nine substrate mints satisfy
// that contract.  Inventory `--check` confirms.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path; substrate's
//              std::optional return + NSDMI handle init.
//   TypeSafe — using-declarations preserve substrate's concept gates
//              (CtxFitsXMint = IsExecCtx ∧ row_contains_v<…, Init>).
//   NullSafe — WorkloadProfiler takes `const Senses*`; substrate's
//              ctor enforces non-null via its own contract.  Other
//              eight mints take no pointer parameters.
//   MemSafe  — substrate's class types (LockContention, PmuSample,
//              etc.) own their kernel resources (program fd, perf
//              event fd, ringbuffer mmap) via RAII; using-decl
//              forwards ownership through std::optional move.
//   BorrowSafe — std::optional moves out; no aliased mutation.
//   ThreadSafe — observation hubs are read-only at runtime; the
//                Init-tier load is single-threaded by construction.
//   LeakSafe — std::optional<X>'s dtor calls X's dtor which closes
//              the kernel fd / unmaps the ring; using-decl preserves.
//   DetSafe  — Init-tier mints; not on the deterministic-replay
//              path.  Observations are advisory.

#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>
#include <crucible/perf/WorkloadProfiler.h>
// SenseHubV2.h deliberately NOT included — see top-of-file rationale.

#include <type_traits>  // sentinel block

namespace crucible::fixy::perf {

// ═════════════════════════════════════════════════════════════════════
// ── LockContention — futex/spinlock wait-time histogram observer ──
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_lock_contention;
using ::crucible::perf::CtxFitsLockContentionMint;
using ::crucible::perf::LockContention;

// ═════════════════════════════════════════════════════════════════════
// ── PmuSample — hardware PMU counter sampling observer ─────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_pmu_sample;
using ::crucible::perf::CtxFitsPmuSampleMint;
using ::crucible::perf::PmuSample;

// ═════════════════════════════════════════════════════════════════════
// ── SchedSwitch — sched-switch tracepoint timeline observer ────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_sched_switch;
using ::crucible::perf::CtxFitsSchedSwitchMint;
using ::crucible::perf::SchedSwitch;

// ═════════════════════════════════════════════════════════════════════
// ── SchedTpBtf — BTF-typed sched tracepoint observer ───────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_sched_tp_btf;
using ::crucible::perf::CtxFitsSchedTpBtfMint;
using ::crucible::perf::SchedTpBtf;

// ═════════════════════════════════════════════════════════════════════
// ── SenseHub — top-level perf-signal aggregator ────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_sense_hub;
using ::crucible::perf::CtxFitsSenseHubMint;
using ::crucible::perf::SenseHub;

// ═════════════════════════════════════════════════════════════════════
// ── SyscallLatency — per-syscall latency-histogram observer ────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_syscall_latency;
using ::crucible::perf::CtxFitsSyscallLatencyMint;
using ::crucible::perf::SyscallLatency;

// ═════════════════════════════════════════════════════════════════════
// ── SyscallTpBtf — BTF-typed syscall tracepoint observer ───────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::perf::mint_syscall_tp_btf;
using ::crucible::perf::CtxFitsSyscallTpBtfMint;
using ::crucible::perf::SyscallTpBtf;

// ═════════════════════════════════════════════════════════════════════
// ── WorkloadProfiler — composite workload-classifier observer ──────
// ═════════════════════════════════════════════════════════════════════
//
// Two overloads (3-arg default Config / 4-arg explicit Config) ride
// the same `CtxFitsWorkloadProfilerMint` gate.  Using-decl surfaces
// both; SFINAE on the substrate's overload set picks the right one.

using ::crucible::perf::mint_workload_profiler;
using ::crucible::perf::CtxFitsWorkloadProfilerMint;
using ::crucible::perf::WorkloadProfiler;

}  // namespace crucible::fixy::perf

// ─── Dual-export sentinel — FIXY-U-121 ─────────────────────────────
//
// Header-internal identity sentinels for every perf surface item.
// Same recipe as fixy/Warden.h::self_test (FIXY-U-120) — drift
// surfaces at every consumer's include time, NOT only when the test
// TU runs.  Cardinality witness at the tail catches a future
// contributor adding (or removing) a perf mint without updating
// both the using-decl block AND this sentinel.

namespace crucible::fixy::perf::self_test {

// ─── Type-identity witnesses ─────────────────────────────────────
//
// Each `static_assert(std::is_same_v<...>)` proves the fixy:: alias
// resolves to the same class as the substrate; a regression that
// introduces a shadowed local declaration or rewrites the using-
// decl to import a different symbol reds the build at include time.

static_assert(std::is_same_v<
    ::crucible::fixy::perf::LockContention,
    ::crucible::perf::LockContention>,
    "fixy::perf::LockContention must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::PmuSample,
    ::crucible::perf::PmuSample>,
    "fixy::perf::PmuSample must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::SchedSwitch,
    ::crucible::perf::SchedSwitch>,
    "fixy::perf::SchedSwitch must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::SchedTpBtf,
    ::crucible::perf::SchedTpBtf>,
    "fixy::perf::SchedTpBtf must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::SenseHub,
    ::crucible::perf::SenseHub>,
    "fixy::perf::SenseHub must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::SyscallLatency,
    ::crucible::perf::SyscallLatency>,
    "fixy::perf::SyscallLatency must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::SyscallTpBtf,
    ::crucible::perf::SyscallTpBtf>,
    "fixy::perf::SyscallTpBtf must alias substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::perf::WorkloadProfiler,
    ::crucible::perf::WorkloadProfiler>,
    "fixy::perf::WorkloadProfiler must alias substrate.");

// ─── Positive concept admittance (ColdInitCtx — the Init-row carrier)
//
// Each CtxFitsXMint must admit ColdInitCtx (carries Init in its
// effect row).  Substrate ships these asserts at each mint site.

static_assert(::crucible::fixy::perf::CtxFitsLockContentionMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsLockContentionMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsPmuSampleMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsPmuSampleMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSchedSwitchMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsSchedSwitchMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSchedTpBtfMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsSchedTpBtfMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSenseHubMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsSenseHubMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSyscallLatencyMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsSyscallLatencyMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsSyscallTpBtfMint must admit ColdInitCtx.");

static_assert(::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<
    ::crucible::effects::ColdInitCtx>,
    "fixy::perf::CtxFitsWorkloadProfilerMint must admit ColdInitCtx.");

// ─── Negative concept rejection (BgDrainCtx — Bg+Alloc, no Init)
//
// Steady-state drain context carries Row<Bg, Alloc>; cannot stand
// up fresh BPF programs.  Substrate rejects; fixy:: must too.

static_assert(!::crucible::fixy::perf::CtxFitsLockContentionMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsLockContentionMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsPmuSampleMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsPmuSampleMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedSwitchMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsSchedSwitchMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedTpBtfMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsSchedTpBtfMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSenseHubMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsSenseHubMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallLatencyMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsSyscallLatencyMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsSyscallTpBtfMint must reject BgDrainCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<
    ::crucible::effects::BgDrainCtx>,
    "fixy::perf::CtxFitsWorkloadProfilerMint must reject BgDrainCtx.");

// ─── Negative concept rejection (HotFgCtx — empty row, no Init)
//
// Hot foreground carries Row<> (empty); fails Init conjunct trivially.

static_assert(!::crucible::fixy::perf::CtxFitsLockContentionMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsLockContentionMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsPmuSampleMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsPmuSampleMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedSwitchMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsSchedSwitchMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSchedTpBtfMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsSchedTpBtfMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSenseHubMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsSenseHubMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallLatencyMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsSyscallLatencyMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsSyscallTpBtfMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsSyscallTpBtfMint must reject HotFgCtx.");

static_assert(!::crucible::fixy::perf::CtxFitsWorkloadProfilerMint<
    ::crucible::effects::HotFgCtx>,
    "fixy::perf::CtxFitsWorkloadProfilerMint must reject HotFgCtx.");

// ─── Cardinality witness ─────────────────────────────────────────
//
// Eight V1 mint factories surface through `fixy::perf::`.  A ninth
// substrate mint (the V2-hub factory) exists but is deferred to a
// future `fixy::perf::v2::` umbrella once V1/V2 substrate identifier
// collisions are resolved — see the top-of-file SenseHubV2 deferral
// rationale.  Adding a tenth perf mint OR landing the V2 umbrella
// must touch BOTH this constant AND
// `static_assert(perf_mint_cardinality == 8)` in test_fixy_perf.cpp;
// otherwise CI reds.

inline constexpr int perf_mint_cardinality = 8;

}  // namespace crucible::fixy::perf::self_test

// ─── Runtime smoke test ────────────────────────────────────────────
//
// Per FIXY-U-103 discipline (every fixy/ header ships a runtime
// smoke block).  Smoke for perf mints is type-level: we cannot
// actually invoke any of the nine mints from a smoke routine without
// performing real bpf() syscalls (which would fail under most CI
// sandboxes anyway).  The smoke verifies the re-export name
// resolution at runtime context (instantiation is already exercised
// at consteval above; this block ensures the header is reachable
// via runtime-call paths too).

namespace crucible::fixy::perf {

inline void runtime_smoke_test() noexcept {
    // Witness that the concept aliases instantiate at runtime context.
    // Touch one positive + one negative from each of the three classes
    // (Init-tier admit, Bg-tier reject, HotFg-tier reject) to make the
    // smoke representative of the full sentinel coverage.
    constexpr bool admits_cold = CtxFitsSenseHubMint<
        ::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsPmuSampleMint<
        ::crucible::effects::BgDrainCtx>;
    constexpr bool rejects_hot = !CtxFitsLockContentionMint<
        ::crucible::effects::HotFgCtx>;
    (void)admits_cold;
    (void)rejects_bg;
    (void)rejects_hot;
}

}  // namespace crucible::fixy::perf
