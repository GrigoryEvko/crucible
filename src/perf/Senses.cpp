// crucible::perf::Senses — multi-facade aggregator implementation.
//
// Thin wrapper over (SenseHub, SchedSwitch, PmuSample, LockContention,
// SyscallLatency).  Each subprogram is loaded independently; failures
// are tolerated and recorded in coverage().  Move-only.

#include <crucible/perf/Senses.h>

#include <utility>

namespace crucible::perf {

// ─── State — holds 5 std::optional<facade> ──────────────────────────
//
// Each facade is move-only and owns its own BPF object + mmap.
// std::optional gives us the "loaded vs not-loaded" discriminator
// without an extra bool.  Order doesn't matter for correctness;
// kept sense_hub-first to match the documented program order.

struct Senses::State {
    std::optional<SenseHub>       sense_hub;
    std::optional<SchedSwitch>    sched_switch;
    std::optional<PmuSample>      pmu_sample;
    std::optional<LockContention> lock_contention;
    std::optional<SyscallLatency> syscall_latency;
};

Senses::Senses(std::unique_ptr<State> s) noexcept : state_{std::move(s)} {}

Senses::Senses(Senses&&) noexcept            = default;
Senses& Senses::operator=(Senses&&) noexcept = default;
Senses::~Senses() noexcept                   = default;

// ─── load_subset ─────────────────────────────────────────────────────
//
// The actual load routine.  load_all() is just load_subset(SensesMask::all()).
// effects::Init is consumed by-value (1-byte EBO tag) and re-minted
// for each subprogram's load() — Init is the "once at startup" cap,
// and we ARE at startup, so re-minting is sound.

Senses Senses::load_subset(::crucible::effects::Init init,
                           SensesMask which) noexcept {
    auto s = std::make_unique<State>();
    if (!s) {
        // OOM at startup → return empty Senses; caller's coverage()
        // will report all-false.  This is preferable to abort or
        // throwing — Senses is supposed to never fail wholesale.
        return Senses{nullptr};
    }

    if (which.sense_hub)       s->sense_hub       = SenseHub::load(init);
    if (which.sched_switch)    s->sched_switch    = SchedSwitch::load(init);
    if (which.pmu_sample)      s->pmu_sample      = PmuSample::load(init);
    if (which.lock_contention) s->lock_contention = LockContention::load(init);
    if (which.syscall_latency) s->syscall_latency = SyscallLatency::load(init);

    return Senses{std::move(s)};
}

// ─── load_all — convenience wrapper ──────────────────────────────────

Senses Senses::load_all(::crucible::effects::Init init) noexcept {
    return load_subset(init, SensesMask::all());
}

// ─── Accessors ───────────────────────────────────────────────────────

const SenseHub* Senses::sense_hub() const noexcept {
    if (!state_ || !state_->sense_hub.has_value()) return nullptr;
    return &(*state_->sense_hub);
}

const SchedSwitch* Senses::sched_switch() const noexcept {
    if (!state_ || !state_->sched_switch.has_value()) return nullptr;
    return &(*state_->sched_switch);
}

const PmuSample* Senses::pmu_sample() const noexcept {
    if (!state_ || !state_->pmu_sample.has_value()) return nullptr;
    return &(*state_->pmu_sample);
}

const LockContention* Senses::lock_contention() const noexcept {
    if (!state_ || !state_->lock_contention.has_value()) return nullptr;
    return &(*state_->lock_contention);
}

const SyscallLatency* Senses::syscall_latency() const noexcept {
    if (!state_ || !state_->syscall_latency.has_value()) return nullptr;
    return &(*state_->syscall_latency);
}

// ─── coverage ────────────────────────────────────────────────────────

CoverageReport Senses::coverage() const noexcept {
    CoverageReport r;
    if (state_) {
        r.sense_hub_attached       = state_->sense_hub.has_value();
        r.sched_switch_attached    = state_->sched_switch.has_value();
        r.pmu_sample_attached      = state_->pmu_sample.has_value();
        r.lock_contention_attached = state_->lock_contention.has_value();
        r.syscall_latency_attached = state_->syscall_latency.has_value();
    }
    return r;
}

} // namespace crucible::perf
