#pragma once

// Unified realtime-policy description for Crucible.
//
// One `Policy` struct, consumed by two clients:
//   • the Keeper daemon (src/keeper/…) at startup — applies `production()`
//   • the bench harness (bench/bench_harness.h) — applies the same policy
//     via `bench::Run::hardening(policy)` so measurements reflect what
//     prod actually does, not dev defaults
//
// The design intent is in misc/CRUCIBLE.md §16. Each knob is wired in
// Hardening.h's `apply()` function, which returns an RAII guard that
// restores prior state on destruction (so benches can opt in for the
// duration of a single Run without mutating process-wide state past
// measure()).
//
// Profile factory functions (production / dev_quiet / none) encode the
// sensible defaults for the three primary use cases. Callers that need
// something unusual build a Policy by copying a profile and overriding
// specific fields.

#include "Topology.h"

#include <cstdint>

namespace crucible::rt {

// Linux scheduler class — selected via sched_setattr. Fifo/Deadline
// require CAP_SYS_NICE (or RLIMIT_RTPRIO).
enum class SchedClass : uint8_t {
    Other,      // SCHED_OTHER — default, time-shared
    Batch,      // SCHED_BATCH — throughput-oriented, less preemption
    Idle,       // SCHED_IDLE — lowest, only runs when CPU otherwise idle
    Fifo,       // SCHED_FIFO — real-time, starves below same-prio
    RoundRobin, // SCHED_RR   — real-time with time-slice among same-prio
    Deadline,   // SCHED_DEADLINE — CBS-admitted (runtime, deadline, period)
};

enum class ThreadClass : uint8_t {
    Hot,   // dispatch / compiled replay — one per compute chip
    Warm,  // background compile, graph build, memory planner, trace drain
    Cold,  // gossip, Cipher writer, Augur, health, self-update
};

// What happens when a privileged knob can't be set (missing capability,
// sysfs read-only, kernel too old). A production Keeper typically wants
// `DegradeAndWarn` so it still starts on a half-configured node; a CI
// bench wants `Strict` to fail loud.
enum class OnMissingCap : uint8_t {
    DegradeAndWarn, // log a warning, continue with whatever worked
    Strict,         // refuse to apply; return an error
};

struct Policy {
    // ── HOT dispatcher thread ──────────────────────────────────────
    // Master switch: if false, apply() is a no-op for the HOT thread
    // (no pinning, no scheduler change, no mlock on hot regions).
    bool          hot_enabled          = true;
    // Which kernel scheduling class to request via sched_setattr for
    // the HOT thread. See the SchedClass enum for per-value semantics.
    SchedClass    hot_sched            = SchedClass::Other;
    // SCHED_DEADLINE parameters (ns). Kernel CBS admits if (runtime /
    // period) across all deadline threads ≤ 1.
    uint64_t      hot_runtime_ns       = 500'000;
    uint64_t      hot_deadline_ns      = 1'000'000;
    uint64_t      hot_period_ns        = 1'000'000;
    // Priority for Fifo / RR (1..99; higher = more urgent).
    int           hot_rt_priority      = 50;
    // Core-selection strategy; see Topology.h.
    CoreSelector  hot_core             = {};

    // ── WARM / COLD threads ────────────────────────────────────────
    // setpriority() nice value for WARM threads (background compile,
    // graph build, memory planner, trace drain). 0 = default.
    int           warm_nice            = 0;
    // setpriority() nice value for COLD threads (gossip, Cipher
    // writer, Augur, health, self-update). Higher = less CPU share.
    int           cold_nice            = 10;

    // ── Memory ─────────────────────────────────────────────────────
    // mlock2(MLOCK_ONFAULT) regions that Crucible explicitly registers
    // (PoolAllocator, MemoryPlan pool backing, TraceRing, KernelCache).
    // Individual mlock calls happen in the respective components; this
    // flag toggles whether apply() calls mlock2 at all.
    bool          mlock_hot_regions    = true;

    // madvise(MADV_HUGEPAGE) on MemoryPlan pool backing — encourages
    // 2 MB hugepages on big mostly-static mappings.
    bool          thp_hint_pools       = true;

    // prctl(PR_SET_THP_DISABLE) — globally opt the process out of
    // khugepaged. Subsequent MADV_HUGEPAGE on specific regions still
    // works; only the background collapse daemon is disabled.
    bool          disable_thp_global   = false;

    // madvise(MADV_COLLAPSE) — synchronously build hugepages now.
    // Kernel ≥ 6.1. No-op on older kernels.
    bool          thp_collapse_now     = false;

    // During Meridian, walk every registered hot region touching one
    // byte per 4KB page to populate page tables before measurement.
    // Individual component inits perform the walk; this flag lets the
    // Keeper signal whether the prefault phase ran.
    bool          prefault_hot_regions = true;

    // ── CPU frequency / C-states ───────────────────────────────────
    // Write scaling_min_freq == scaling_max_freq on the HOT core.
    // Requires write access to /sys/devices/system/cpu/.../cpufreq/.
    bool          lock_frequency       = false;
    // Write 1 to cpuidle/stateN/disable for N > 0 (stays in C0/C1).
    bool          disable_c_states     = false;

    // ── I/O ────────────────────────────────────────────────────────
    // Cipher hot-tier writer uses io_uring with SQPOLL when available.
    // The Cipher subsystem reads this flag at init.
    bool          io_uring_sqpoll      = true;
    // CNTP uses RDMA verbs via UCX when available; falls back to TCP
    // when the nic isn't RDMA-capable.
    bool          rdma_for_comm        = true;

    // ── Watchdog ───────────────────────────────────────────────────
    // When SCHED_DEADLINE and the kernel signals SIGXCPU (deadline
    // miss), count misses in a rolling window; on breach, downgrade
    // HOT → FIFO → OTHER.
    uint32_t      deadline_miss_budget = 10;
    uint32_t      watchdog_window_sec  = 60;

    // ── Fallback semantics ─────────────────────────────────────────
    // How apply() behaves when a privileged knob fails (missing
    // capability, sysfs read-only, unsupported kernel). See enum doc.
    OnMissingCap  on_missing_capability = OnMissingCap::DegradeAndWarn;

    // ── Profiles ───────────────────────────────────────────────────

    // Production Keeper on a well-provisioned cluster node. Everything
    // on; SCHED_DEADLINE for the dispatch thread. Degrade-and-warn if
    // a capability is missing (so the Keeper still boots on a fallback
    // node and reports DEGRADED upstream).
    [[nodiscard]] static constexpr Policy production() noexcept {
        Policy p;
        p.hot_enabled           = true;
        p.hot_sched             = SchedClass::Deadline;
        p.mlock_hot_regions     = true;
        p.thp_hint_pools        = true;
        p.disable_thp_global    = true;
        p.thp_collapse_now      = true;
        p.lock_frequency        = true;
        p.disable_c_states      = true;
        p.io_uring_sqpoll       = true;
        p.rdma_for_comm         = true;
        p.on_missing_capability = OnMissingCap::DegradeAndWarn;
        return p;
    }

    // Dev laptop: pinned but scheduler-friendly, no frequency fiddling,
    // no SCHED_DEADLINE (would wedge the laptop if the bench spins).
    // Used by the bench harness on dev machines; also the default if
    // an operator passes no profile name.
    [[nodiscard]] static constexpr Policy dev_quiet() noexcept {
        Policy p;
        p.hot_enabled           = true;
        p.hot_sched             = SchedClass::Other;  // safe on laptops
        p.mlock_hot_regions     = true;   // usually available to users
        p.thp_hint_pools        = false;  // let the kernel decide
        p.disable_thp_global    = false;
        p.thp_collapse_now      = false;
        p.lock_frequency        = false;  // respect the user's governor
        p.disable_c_states      = false;  // don't burn battery
        p.io_uring_sqpoll       = false;  // not worth on a laptop
        p.rdma_for_comm         = false;
        p.on_missing_capability = OnMissingCap::DegradeAndWarn;
        return p;
    }

    // Opt out entirely. For debugging ("does my bug reproduce without
    // any hardening?") or for shells where realtime isn't wanted.
    [[nodiscard]] static constexpr Policy none() noexcept {
        Policy p;
        p.hot_enabled           = false;
        p.mlock_hot_regions     = false;
        p.thp_hint_pools        = false;
        p.disable_thp_global    = false;
        p.thp_collapse_now      = false;
        p.lock_frequency        = false;
        p.disable_c_states      = false;
        p.prefault_hot_regions  = false;
        p.io_uring_sqpoll       = false;
        p.rdma_for_comm         = false;
        return p;
    }
};

// One cache line of hot fields (scheduler / pinning) + cold tail
// (mlock, THP hints, watchdog). The whole struct is passed by value
// to apply() once per component init, so keep it small and trivially
// copyable. If this fires, something was added that probably doesn't
// belong in Policy — consider a separate config struct.
static_assert(sizeof(Policy) < 256);

} // namespace crucible::rt
