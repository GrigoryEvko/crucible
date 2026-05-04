#pragma once

// crucible::perf::Senses — multi-facade aggregator for the GAPS-004
// BPF observability suite.  Single entry point that loads any subset
// of (SenseHub, SchedSwitch, PmuSample, LockContention, SyscallLatency)
// in one call and reports per-program coverage.
//
// ─── DESIGN INTENT ────────────────────────────────────────────────────
//
// After GAPS-004{a,b,c,d,e} shipped 5 per-program facades, every
// caller that wanted "all the BPF observability" had to:
//
//   auto h1 = SenseHub::load(Init{});
//   auto h2 = SchedSwitch::load(Init{});
//   auto h3 = PmuSample::load(Init{});
//   auto h4 = LockContention::load(Init{});
//   auto h5 = SyscallLatency::load(Init{});
//
// Plus track which loaded.  Plus coordinate teardown.  Senses replaces
// that with one call:
//
//   auto s = Senses::load_all(crucible::effects::Init{});
//   auto cov = s.coverage();          // diagnostic: which loaded
//   if (auto* h = s.sense_hub())      { /* use it */ }
//   if (auto* p = s.pmu_sample())     { /* use it */ }
//
// load_all() never fails as a whole — each subprogram is loaded
// independently, failures recorded in coverage().  load_subset()
// loads only the masked subset (for callers that want the cost
// budget of a known facade list).
//
// ─── COST DISCIPLINE ──────────────────────────────────────────────────
//
// Senses is a thin C++ wrapper — zero per-event runtime cost.  Each
// subprogram retains its own cost budget; aggregating doesn't change
// the per-event model.  Senses adds one-time load-cost (~50-200ms per
// program × 7 programs for libbpf attach) and one accessor-call cost
// (single pointer comparison) per query.
//
// Per-facade cost budgets (steady state):
//   • SenseHub:        ~0.2-0.6 % CPU (system-wide sched_switch hook)
//   • SchedSwitch:     ~0.05-0.2 % CPU (per-tenant timeline ring)
//   • PmuSample:       ~0.01-0.1 % CPU (perf event sampling at 99 Hz)
//   • LockContention:  ~0.01-0.05 % CPU (futex enter/exit pairs)
//   • SyscallLatency:  ~0.05-0.2 % CPU (raw_syscalls/sys_enter+exit)
//   • SchedTpBtf:      ~0.04-0.15 % CPU (BTF tp_btf/sched_switch — same
//                       events as SchedSwitch, ~30 % cheaper per event)
//   • SyscallTpBtf:    ~0.04-0.15 % CPU (BTF tp_btf/sys_{enter,exit})
//
// Senses::load_all() therefore costs ~0.4-1.5 % CPU steady-state with
// all 7 attached.  load_subset() lets callers stay under 0.5 % by
// skipping the expensive ones (BTF facades observe the same events
// as the legacy ones — loading both in production doubles cost).
//
// ─── HS14 NEG-COMPILE FIXTURES ────────────────────────────────────────
//
// • neg_perf_senses_load_no_cap.cpp     — Senses::load_all() without arg
// • neg_perf_senses_load_wrong_cap.cpp  — pass effects::Bg{} instead of Init{}

#include <crucible/effects/Capabilities.h>
#include <crucible/perf/LockContention.h>
#include <crucible/perf/PmuSample.h>
#include <crucible/perf/SchedSwitch.h>
#include <crucible/perf/SchedTpBtf.h>
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SyscallLatency.h>
#include <crucible/perf/SyscallTpBtf.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace crucible::perf {

// ─── SensesMask — pick which subset to load ──────────────────────────
//
// Default-constructed = nothing selected.  Use designated initializers
// at call sites:
//
//   auto s = Senses::load_subset(Init{}, SensesMask{
//       .sense_hub      = true,
//       .pmu_sample     = true,
//       .syscall_latency= true});
//
// 1-byte struct, EBO-friendly, fits in a single register at call site.
//
// GAPS-004f wire-in (2026-05-04): added sched_tp_btf + syscall_tp_btf
// for the BTF-typed parallel facades (~30% lower per-event cost on
// kernels with CONFIG_DEBUG_INFO_BTF=y).  load_all() now loads all 7;
// callers wanting cost-discipline pick the legacy-only / BTF-only
// subset explicitly.  Both BTF and legacy facades observe the same
// kernel events, so loading both records duplicates — that's fine
// at the API level (each facade has its own counters and timeline)
// but doubles the libbpf attach cost (~50 ms per program).

struct SensesMask {
    bool sense_hub       : 1 = false;
    bool sched_switch    : 1 = false;
    bool pmu_sample      : 1 = false;
    bool lock_contention : 1 = false;
    bool syscall_latency : 1 = false;
    bool sched_tp_btf    : 1 = false;
    bool syscall_tp_btf  : 1 = false;

    [[nodiscard]] static constexpr SensesMask all() noexcept {
        return SensesMask{
            .sense_hub       = true,
            .sched_switch    = true,
            .pmu_sample      = true,
            .lock_contention = true,
            .syscall_latency = true,
            .sched_tp_btf    = true,
            .syscall_tp_btf  = true,
        };
    }

    [[nodiscard]] constexpr bool any() const noexcept {
        return sense_hub || sched_switch || pmu_sample ||
               lock_contention || syscall_latency ||
               sched_tp_btf || syscall_tp_btf;
    }
};

// ─── CoverageReport ──────────────────────────────────────────────────
//
// Per-program load status, returned by Senses::coverage().  Used by
// bench harness banner ("loaded: SenseHub PmuSample") and Augur for
// drift attribution ("can't read CPU stalls — PmuSample is missing").

struct CoverageReport {
    bool sense_hub_attached       = false;
    bool sched_switch_attached    = false;
    bool pmu_sample_attached      = false;
    bool lock_contention_attached = false;
    bool syscall_latency_attached = false;
    bool sched_tp_btf_attached    = false;
    bool syscall_tp_btf_attached  = false;

    [[nodiscard]] std::size_t attached_count() const noexcept {
        return (sense_hub_attached       ? 1u : 0u)
             + (sched_switch_attached    ? 1u : 0u)
             + (pmu_sample_attached      ? 1u : 0u)
             + (lock_contention_attached ? 1u : 0u)
             + (syscall_latency_attached ? 1u : 0u)
             + (sched_tp_btf_attached    ? 1u : 0u)
             + (syscall_tp_btf_attached  ? 1u : 0u);
    }
};

// ─── class Senses ────────────────────────────────────────────────────
//
// Move-only — owns N sub-facades each of which owns BPF object + mmap.
// Constructor private; only the load_* factories build instances.

class Senses {
public:
    // Load every available subprogram.  Per-subprogram failures
    // (kernel too old, missing CAP, libbpf load error) are tolerated;
    // each is recorded in coverage().  Returns a Senses with whatever
    // subset succeeded — never returns std::nullopt at this level
    // because partial loads are still useful.
    [[nodiscard]] static Senses
        load_all(::crucible::effects::Init) noexcept;

    // Load only the masked subset.  Callers that want predictable cost
    // (e.g. ≤ 0.5 % CPU budget) use this with SensesMask listing only
    // low-rate facades like SenseHub + PmuSample.
    [[nodiscard]] static Senses
        load_subset(::crucible::effects::Init, SensesMask which) noexcept;

    // Per-subprogram accessors.  Return non-null pointer if the
    // subprogram loaded successfully, nullptr otherwise.  Lifetime
    // tied to the Senses instance — do not retain past Senses
    // destruction.  Const-only — sub-facades are read-only at this
    // surface (mutating ops live on the facade types themselves).
    [[nodiscard]] const SenseHub*       sense_hub()       const noexcept;
    [[nodiscard]] const SchedSwitch*    sched_switch()    const noexcept;
    [[nodiscard]] const PmuSample*      pmu_sample()      const noexcept;
    [[nodiscard]] const LockContention* lock_contention() const noexcept;
    [[nodiscard]] const SyscallLatency* syscall_latency() const noexcept;
    // GAPS-004f: BTF-typed parallel facades (lower per-event cost on
    // kernels with CONFIG_DEBUG_INFO_BTF=y + ≥ 5.5).
    [[nodiscard]] const SchedTpBtf*     sched_tp_btf()    const noexcept;
    [[nodiscard]] const SyscallTpBtf*   syscall_tp_btf()  const noexcept;

    // Coverage report — which subprograms attached, used by
    // bench harness banner and Augur drift-attribution.
    [[nodiscard]] CoverageReport coverage() const noexcept;

    // Move-only — owns 7 std::optional<facade>, each with a BPF object
    // + mmap.  Copying would double-close the BPF objects on
    // destruction.  Same delete-with-reason discipline as every
    // per-program facade in the GAPS-004 series.
    Senses(const Senses&)            =
        delete("Senses owns 7 BPF objects + mmaps; copying would double-close");
    Senses& operator=(const Senses&) =
        delete("Senses owns 7 BPF objects + mmaps; copying would double-close");
    Senses(Senses&&) noexcept;
    Senses& operator=(Senses&&) noexcept;
    ~Senses() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;

    explicit Senses(std::unique_ptr<State>) noexcept;
};

} // namespace crucible::perf
