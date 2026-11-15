#include <crucible/perf/SenseHub.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::perf {

namespace {
struct SenseHubCountersTag {};
}  // namespace

struct SenseHub::State : ::foundation::NonMovable<SenseHub::State> {
    static constexpr int kMaxLinks = 64;

    detail::BpfObject<kMaxLinks> object{};
    ::fixy::Option<detail::ReadOnlyMapping<SenseHubCountersTag>> counters{};
};

SenseHub::SenseHub(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
SenseHub::SenseHub(SenseHub&&) noexcept = default;
SenseHub& SenseHub::operator=(SenseHub&&) noexcept = default;
SenseHub::~SenseHub() = default;

::fixy::Option<SenseHub> load_sense_hub(::fixy::InitLoadCtx const& ctx) noexcept {
    constexpr detail::ForeignName facade = CRUCIBLE_PERF_FOREIGN_NAME("BPF sense hub");
    const detail::LoadSpec spec{
        .facade = facade,
        .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_senses"),
        .bytecode = detail::sense_hub_bpf_object(),
        .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel missing "
                         "every tracepoint, or lacking CAP_PERFMON or CAP_DAC_READ_SEARCH)",
    };
    using State = detail::FacadeDoor::State<SenseHub>;
    return detail::FacadeDoor::load<SenseHub>(ctx, [&ctx, &facade, &spec](State& state) noexcept {
        if (!state.object.load(ctx, spec)) return false;
        state.counters = detail::map_array<SenseHubCountersTag>(
            ctx, state.object, facade.text, CRUCIBLE_PERF_FOREIGN_NAME("counters"), NUM_COUNTERS * sizeof(uint64_t));
        if (state.counters.is_none()) return false;
        state.object.report_partial(facade.text);
        return true;
    });
}

Snapshot SenseHub::read() const noexcept {
    Snapshot snapshot{};
    for (::fixy::Box<State> const& state : state_) {
        for (detail::ReadOnlyMapping<SenseHubCountersTag> const& mapped : state->counters) {
            // The BPF producer bumps each counter with a full-barrier add, so
            // an acquire load here makes every kernel-side store that
            // precedes a bump visible, and stops the compiler hoisting later
            // reads above it.  view_at of the mapping checks the bounds and
            // the alignment, and it starts the lifetime of the counters.
            for (::fixy::View<const uint64_t, NUM_COUNTERS> const counters :
                 mapped.template view_at<const uint64_t, NUM_COUNTERS>(0)) {
                for (::fixy::Index<NUM_COUNTERS> const i : counters.indices()) {
                    snapshot.counters.at(i) = __atomic_load_n(&counters.at(i), __ATOMIC_ACQUIRE);
                }
            }
        }
    }
    return snapshot;
}

::fixy::Borrowed<const volatile uint64_t, SenseHub> SenseHub::counters_view() const noexcept {
    for (::fixy::Box<State> const& state : state_) {
        for (detail::ReadOnlyMapping<SenseHubCountersTag> const& mapped : state->counters) {
            for (::fixy::View<const uint64_t, NUM_COUNTERS> const counters :
                 mapped.template view_at<const uint64_t, NUM_COUNTERS>(0)) {
                return ::fixy::Borrowed<const volatile uint64_t, SenseHub>{counters};
            }
        }
    }
    return ::fixy::Borrowed<const volatile uint64_t, SenseHub>{};
}

::fixy::Refined<::fixy::bounded_above<64>, std::size_t> SenseHub::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::Refined<::fixy::bounded_above<64>, std::size_t> SenseHub::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

}  // namespace crucible::perf
