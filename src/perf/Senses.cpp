// crucible::perf::Senses — multi-facade aggregator implementation.
//
// Thin wrapper over 7 BPF facades (5 legacy + 2 BTF parallel).  Each
// subprogram is loaded independently; failures are tolerated and
// recorded in coverage().  Move-only.

#include <crucible/perf/Senses.h>

#include <utility>

namespace crucible::perf {

// State definition moved into Senses.h to eliminate the noexcept-path
// heap allocation (fixy-A5-026 / FIXY-U-088).  See the policy block
// at the State definition site for the discipline rationale.

Senses::Senses(std::optional<State> s) noexcept : state_{std::move(s)} {}

// ─── Move ctor + move assign — explicit, not =default ────────────────
//
// `=default` on a class whose only member is `std::optional<State>` is
// WRONG.  `std::optional<T>::operator=(optional&&)` does NOT reset the
// source to `std::nullopt` — it moves the contained `T` but leaves the
// source's `has_value()` returning `true` (with a moved-from `T`
// inside).  Same for the move-ctor: source remains engaged.
//
// Pre-FIXY-U-088 / fixy-A5-026 this class held `std::unique_ptr<State>`
// whose move-ctor IS source-nullifying — coverage()/accessors on the
// moved-from instance returned zero/nullptr via the null-pointer guard.
// U-088 replaced unique_ptr with inline `std::optional<State>` to
// eliminate the noexcept-Init-path heap allocation; the move semantics
// regression was unintended.  `std::exchange(other.state_,
// std::nullopt)` restores the source-nullifying contract by atomically
// (from the reader's perspective) moving the value out and resetting
// the source to `nullopt`.
//
// Post-move source-side contract — enforced by test_perf_senses_smoke
// (step 8) and the design intent at Senses.h:152 ("owns N sub-facades
// each of which owns BPF object + mmap"):
//   • coverage().attached_count() == 0
//   • every accessor returns nullptr
//
// Both follow from `state_ == std::nullopt` via the existing
// `if (!state_) return ...` guards in the accessor + coverage paths.

Senses::Senses(Senses&& other) noexcept
    : state_{std::exchange(other.state_, std::nullopt)} {}

Senses& Senses::operator=(Senses&& other) noexcept {
    if (this != &other) {
        state_ = std::exchange(other.state_, std::nullopt);
    }
    return *this;
}

Senses::~Senses() noexcept = default;

// ─── load_subset ─────────────────────────────────────────────────────
//
// The actual load routine.  load_all() is just load_subset(SensesMask::all()).
// effects::Init is consumed by-value (1-byte EBO tag) and re-minted
// for each subprogram's load() — Init is the "once at startup" cap,
// and we ARE at startup, so re-minting is sound.

Senses Senses::load_subset(::crucible::effects::Init init,
                           SensesMask which) noexcept {
    // fixy-A5-026 / FIXY-U-088: State is stack-constructed (was nothrow
    // new + unique_ptr).  noexcept Init-path forbids heap allocation:
    // even nothrow-new is a system-call boundary that can fail under
    // pressure, and silently degrading to all-false coverage on OOM
    // was a hidden failure mode.  Stack construction never fails;
    // NRVO moves the State into the returned Senses without copy.
    State s{};

    if (which.sense_hub)       s.sense_hub       = SenseHub::load(init);
    if (which.sched_switch)    s.sched_switch    = SchedSwitch::load(init);
    if (which.pmu_sample)      s.pmu_sample      = PmuSample::load(init);
    if (which.lock_contention) s.lock_contention = LockContention::load(init);
    if (which.syscall_latency) s.syscall_latency = SyscallLatency::load(init);
    if (which.sched_tp_btf)    s.sched_tp_btf    = SchedTpBtf::load(init);
    if (which.syscall_tp_btf)  s.syscall_tp_btf  = SyscallTpBtf::load(init);

    return Senses{std::optional<State>{std::move(s)}};
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

const SchedTpBtf* Senses::sched_tp_btf() const noexcept {
    if (!state_ || !state_->sched_tp_btf.has_value()) return nullptr;
    return &(*state_->sched_tp_btf);
}

const SyscallTpBtf* Senses::syscall_tp_btf() const noexcept {
    if (!state_ || !state_->syscall_tp_btf.has_value()) return nullptr;
    return &(*state_->syscall_tp_btf);
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
        r.sched_tp_btf_attached    = state_->sched_tp_btf.has_value();
        r.syscall_tp_btf_attached  = state_->syscall_tp_btf.has_value();
    }
    return r;
}

} // namespace crucible::perf
