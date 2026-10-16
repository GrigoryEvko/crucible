#include <crucible/perf/Senses.h>

#include <utility>

namespace crucible::perf {

Senses::Senses(std::optional<State> s) noexcept : state_{std::move(s)} {}

// These two are written out rather than defaulted.  A defaulted move leaves
// the source engaged, because the move operations of std::optional move the
// contained value but do not reset has_value().  The exchange resets the
// source to nullopt, so the guards in the accessors and in coverage() report
// a moved-from Senses as owning nothing.

Senses::Senses(Senses&& other) noexcept : state_{std::exchange(other.state_, std::nullopt)} {}

Senses& Senses::operator=(Senses&& other) noexcept {
    if (this != &other) {
        state_ = std::exchange(other.state_, std::nullopt);
    }
    return *this;
}

Senses::~Senses() noexcept = default;

// The startup load context admits every subprogram load below.  Its row
// carries Block, and each load waits in the kernel for the verifier.
//
// A build with no BPF substrate has no facade to load.  The load then gives
// a Senses that attaches nothing, and each accessor gives nullptr.

Senses Senses::load_subset(::fixy::InitLoadCtx const& init, SensesMask which) noexcept {
    // State is stack-constructed.  A noexcept load path must not allocate: a
    // nothrow new that fails under memory pressure would degrade silently
    // into an all-false coverage report.
    State s{};

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (which.sense_hub) s.sense_hub = SenseHub::load(init);
    if (which.sched_switch) s.sched_switch = SchedSwitch::load(init);
    if (which.pmu_sample) s.pmu_sample = PmuSample::load(init);
    if (which.lock_contention) s.lock_contention = LockContention::load(init);
    if (which.syscall_latency) s.syscall_latency = SyscallLatency::load(init);
    if (which.sched_tp_btf) s.sched_tp_btf = SchedTpBtf::load(init);
    if (which.syscall_tp_btf) s.syscall_tp_btf = SyscallTpBtf::load(init);
#else
    static_cast<void>(init);
    static_cast<void>(which);
#endif

    return Senses{std::optional<State>{std::move(s)}};
}

Senses Senses::load_all(::fixy::InitLoadCtx const& init) noexcept { return load_subset(init, SensesMask::all()); }

const SenseHub* Senses::sense_hub() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->sense_hub.has_value()) return nullptr;
    return &(*state_->sense_hub);
#else
    return nullptr;
#endif
}

const SchedSwitch* Senses::sched_switch() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->sched_switch.has_value()) return nullptr;
    return &(*state_->sched_switch);
#else
    return nullptr;
#endif
}

const PmuSample* Senses::pmu_sample() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->pmu_sample.has_value()) return nullptr;
    return &(*state_->pmu_sample);
#else
    return nullptr;
#endif
}

const LockContention* Senses::lock_contention() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->lock_contention.has_value()) return nullptr;
    return &(*state_->lock_contention);
#else
    return nullptr;
#endif
}

const SyscallLatency* Senses::syscall_latency() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->syscall_latency.has_value()) return nullptr;
    return &(*state_->syscall_latency);
#else
    return nullptr;
#endif
}

const SchedTpBtf* Senses::sched_tp_btf() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->sched_tp_btf.has_value()) return nullptr;
    return &(*state_->sched_tp_btf);
#else
    return nullptr;
#endif
}

const SyscallTpBtf* Senses::syscall_tp_btf() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (!state_ || !state_->syscall_tp_btf.has_value()) return nullptr;
    return &(*state_->syscall_tp_btf);
#else
    return nullptr;
#endif
}

CoverageReport Senses::coverage() const noexcept {
    CoverageReport r;
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (state_) {
        r.sense_hub_attached = state_->sense_hub.has_value();
        r.sched_switch_attached = state_->sched_switch.has_value();
        r.pmu_sample_attached = state_->pmu_sample.has_value();
        r.lock_contention_attached = state_->lock_contention.has_value();
        r.syscall_latency_attached = state_->syscall_latency.has_value();
        r.sched_tp_btf_attached = state_->sched_tp_btf.has_value();
        r.syscall_tp_btf_attached = state_->syscall_tp_btf.has_value();
    }
#endif
    return r;
}

}  // namespace crucible::perf
