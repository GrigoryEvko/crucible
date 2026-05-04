// crucible::perf::SenseHubV2 — userspace facade implementation (STUB).
//
// STATUS: PROMOTED ARCHITECTURAL SURFACE, IMPLEMENTATION PENDING.
//
// This .cpp ships ONLY the symbol skeletons needed for the build to
// link when CRUCIBLE_SENSE_HUB_V2=ON.  load() returns nullopt — v1's
// SenseHub remains the active production loader during the transition.
//
// The full implementation (mirror of SenseHub.cpp's 7-step Phase
// loader, plus meta-map publication, counters/gauges mmap, ProcGauges
// integration) lands in a subsequent PR.

#include <crucible/perf/SenseHubV2.h>
#include <crucible/perf/ProcGauges.h>

#include <utility>

namespace crucible::perf {

// ─── State holds the loaded BPF object + mmap pointers + ProcGauges
// instance.  Empty in the stub; populated by the future load() impl. ─

struct SenseHubV2::State {
    // Future fields (TODO):
    //   bpf_object*               bpf;
    //   std::array<bpf_link*, N>  links;
    //   volatile uint64_t*        counters_mmap;
    //   volatile uint64_t*        gauges_mmap;
    //   sense_meta                meta;
    //   ProcGauges                proc_gauges;
    //   CoverageReport            coverage;
    int placeholder = 0;
};

SenseHubV2::SenseHubV2(std::unique_ptr<State> s) noexcept
    : state_{std::move(s)} {}

SenseHubV2::SenseHubV2(SenseHubV2&&) noexcept            = default;
SenseHubV2& SenseHubV2::operator=(SenseHubV2&&) noexcept = default;
SenseHubV2::~SenseHubV2() noexcept                       = default;

std::optional<SenseHubV2>
SenseHubV2::load(::crucible::effects::Init) noexcept {
    // STUB: full impl pending.  v1 SenseHub remains the production
    // loader.  Returning nullopt here means consumers that try to use
    // v2 today get a clean "unavailable" signal instead of a broken
    // half-loaded state.
    return std::nullopt;
}

CounterSnapshot SenseHubV2::read_counters() const noexcept {
    return CounterSnapshot{};  // all zeros
}

GaugeSnapshot SenseHubV2::read_gauges() const noexcept {
    return GaugeSnapshot{};  // all zeros
}

safety::Borrowed<const volatile uint64_t, SenseHubV2>
SenseHubV2::counters_view() const noexcept {
    return {nullptr, 0};
}

safety::Borrowed<const volatile uint64_t, SenseHubV2>
SenseHubV2::gauges_view() const noexcept {
    return {nullptr, 0};
}

CoverageReport SenseHubV2::coverage() const noexcept {
    return CoverageReport{};  // all default — nothing loaded yet
}

} // namespace crucible::perf
