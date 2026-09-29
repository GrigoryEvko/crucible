#include <crucible/perf/SenseHub.h>

#include <crucible/perf/detail/BpfHub.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

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
    std::optional<detail::ReadOnlyMapping<SenseHubCountersTag>> counters{};
};

SenseHub::SenseHub() noexcept = default;
SenseHub::SenseHub(SenseHub&&) noexcept = default;
SenseHub& SenseHub::operator=(SenseHub&&) noexcept = default;
SenseHub::~SenseHub() = default;

std::optional<SenseHub> SenseHub::load(::fixy::InitLoadCtx const& ctx) noexcept {
    constexpr const char* facade = "BPF sense hub";
    const detail::LoadSpec spec{
        .facade = facade,
        .object_name = "crucible_senses",
        .bytecode = std::span{sense_hub_bpf_bytecode, static_cast<std::size_t>(sense_hub_bpf_bytecode_len)},
        .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel missing "
                         "every tracepoint, or lacking CAP_PERFMON or CAP_DAC_READ_SEARCH)",
    };
    auto state = std::make_unique<State>();
    if (!state->object.load(spec)) return std::nullopt;
    state->counters =
        detail::map_array<SenseHubCountersTag>(ctx, state->object, facade, "counters", NUM_COUNTERS * sizeof(uint64_t));
    if (!state->counters) return std::nullopt;
    state->object.report_partial(facade);

    SenseHub hub;
    hub.state_ = std::move(state);
    return hub;
}

Snapshot SenseHub::read() const noexcept {
    Snapshot snapshot;
    if (state_ == nullptr || !state_->counters) return snapshot;
    // The BPF producer bumps each counter with a full-barrier add, so an
    // acquire load here makes every kernel-side store that precedes a bump
    // visible, and stops the compiler hoisting later reads above it.
    // __atomic_load_n through a volatile-qualified pointer is a GCC
    // extension.
    const volatile uint64_t* __restrict src = std::bit_cast<const volatile uint64_t*>(state_->counters->data());
    for (uint32_t i = 0; i < NUM_COUNTERS; ++i) {
        snapshot.counters[i] = __atomic_load_n(&src[i], __ATOMIC_ACQUIRE);
    }
    return snapshot;
}

::fixy::Borrowed<const volatile uint64_t, SenseHub> SenseHub::counters_view() const noexcept {
    if (state_ == nullptr || !state_->counters) {
        return ::fixy::Borrowed<const volatile uint64_t, SenseHub>{};
    }
    return ::fixy::Borrowed<const volatile uint64_t, SenseHub>{
        std::bit_cast<volatile uint64_t*>(state_->counters->data()), NUM_COUNTERS};
}

::fixy::Refined<::fixy::bounded_above<64>, std::size_t> SenseHub::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<64>, std::size_t> SenseHub::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

}  // namespace crucible::perf
