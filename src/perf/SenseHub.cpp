#include <crucible/perf/SenseHub.h>

#include <crucible/perf/detail/BpfHub.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

extern "C" {
extern const unsigned char sense_hub_bpf_bytecode[];
extern const unsigned int sense_hub_bpf_bytecode_len;
}

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
        .bytecode = std::span{sense_hub_bpf_bytecode, static_cast<std::size_t>(sense_hub_bpf_bytecode_len)},
        .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel missing "
                         "every tracepoint, or lacking CAP_PERFMON or CAP_DAC_READ_SEARCH)",
    };
    using State = detail::FacadeDoor::State<SenseHub>;
    return detail::FacadeDoor::load<SenseHub>(ctx, [&ctx, &facade, &spec](State& state) noexcept {
        if (!state.object.load(spec)) return false;
        state.counters = detail::map_array<SenseHubCountersTag>(
            ctx, state.object, facade.text, CRUCIBLE_PERF_FOREIGN_NAME("counters"), NUM_COUNTERS * sizeof(uint64_t));
        if (state.counters.is_none()) return false;
        state.object.report_partial(facade.text);
        return true;
    });
}

Snapshot SenseHub::read() const noexcept {
    Snapshot snapshot;
    for (::fixy::Box<State> const& state : state_) {
        for (detail::ReadOnlyMapping<SenseHubCountersTag> const& mapped : state->counters) {
            // The BPF producer bumps each counter with a full-barrier add, so
            // an acquire load here makes every kernel-side store that
            // precedes a bump visible, and stops the compiler hoisting later
            // reads above it.  __atomic_load_n through a volatile-qualified
            // pointer is a GCC extension.
            const volatile uint64_t* __restrict src = std::bit_cast<const volatile uint64_t*>(mapped.data());
            for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
                snapshot.counters[i] = __atomic_load_n(&src[i], __ATOMIC_ACQUIRE);
            }
        }
    }
    return snapshot;
}

::fixy::Borrowed<const volatile uint64_t, SenseHub> SenseHub::counters_view() const noexcept {
    for (::fixy::Box<State> const& state : state_) {
        for (detail::ReadOnlyMapping<SenseHubCountersTag> const& mapped : state->counters) {
            return ::fixy::Borrowed<const volatile uint64_t, SenseHub>{std::bit_cast<volatile uint64_t*>(mapped.data()),
                                                                       NUM_COUNTERS};
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
