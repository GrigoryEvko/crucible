#include <crucible/perf/Senses.h>

#include <utility>

namespace crucible::perf {

namespace {

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
// A borrow of the facade that option holds, or none when it holds none.
template <class Facade>
[[nodiscard]] ::fixy::Option<::fixy::Ref<Facade const>> borrow_of(::fixy::Option<Facade> const& option) noexcept {
    using Borrow = ::fixy::Option<::fixy::Ref<Facade const>>;
    return option.match([](Facade const& facade) noexcept { return Borrow::some(::fixy::to(facade)); },
                        [] noexcept { return Borrow{::fixy::none}; });
}
#endif

}  // namespace

Senses::Senses(::fixy::Option<State> state) noexcept : state_{std::move(state)} {}

Senses::Senses(Senses&&) noexcept = default;

Senses& Senses::operator=(Senses&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

Senses::~Senses() noexcept = default;

// The startup load context admits every subprogram load below.  Its row
// carries Block, and each load waits in the kernel for the verifier.
//
// A build with no BPF substrate has no facade to load.  The load then gives
// a Senses that attaches nothing, and each accessor gives none.

Senses Senses::load_subset(::fixy::InitLoadCtx const& init, SensesMask which) noexcept {
    // State is stack-constructed.  A noexcept load path must not allocate: a
    // failed allocation under memory pressure would degrade silently into an
    // all-false coverage report.
    State state{};

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    if (which.sense_hub) state.sense_hub = load_sense_hub(init);
    if (which.sched_switch) state.sched_switch = load_sched_switch(init);
    if (which.pmu_sample) state.pmu_sample = load_pmu_sample(init);
    if (which.lock_contention) state.lock_contention = load_lock_contention(init);
    if (which.syscall_latency) state.syscall_latency = load_syscall_latency(init);
    if (which.sched_tp_btf) state.sched_tp_btf = load_sched_tp_btf(init);
    if (which.syscall_tp_btf) state.syscall_tp_btf = load_syscall_tp_btf(init);
#else
    static_cast<void>(init);
    static_cast<void>(which);
#endif

    return Senses{::fixy::Option<State>::some(std::move(state))};
}

Senses Senses::load_all(::fixy::InitLoadCtx const& init) noexcept { return load_subset(init, SensesMask::all()); }

::fixy::Option<::fixy::Ref<SenseHub const>> Senses::sense_hub() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.sense_hub);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<SchedSwitch const>> Senses::sched_switch() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.sched_switch);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<PmuSample const>> Senses::pmu_sample() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.pmu_sample);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<LockContention const>> Senses::lock_contention() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.lock_contention);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<SyscallLatency const>> Senses::syscall_latency() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.syscall_latency);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<SchedTpBtf const>> Senses::sched_tp_btf() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.sched_tp_btf);
    }
#endif
    return ::fixy::none;
}

::fixy::Option<::fixy::Ref<SyscallTpBtf const>> Senses::syscall_tp_btf() const noexcept {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        return borrow_of(state.syscall_tp_btf);
    }
#endif
    return ::fixy::none;
}

CoverageReport Senses::coverage() const noexcept {
    CoverageReport report{};
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    for (State const& state : state_) {
        report.sense_hub_attached = state.sense_hub.is_some();
        report.sched_switch_attached = state.sched_switch.is_some();
        report.pmu_sample_attached = state.pmu_sample.is_some();
        report.lock_contention_attached = state.lock_contention.is_some();
        report.syscall_latency_attached = state.syscall_latency.is_some();
        report.sched_tp_btf_attached = state.sched_tp_btf.is_some();
        report.syscall_tp_btf_attached = state.syscall_tp_btf.is_some();
    }
#endif
    return report;
}

}  // namespace crucible::perf
