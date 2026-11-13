#include <crucible/perf/SenseHubV2.h>
#include <crucible/perf/ProcGauges.h>

namespace crucible::perf {

struct SenseHubV2::State {
    int placeholder = 0;
};

SenseHubV2::SenseHubV2(SenseHubV2&&) noexcept = default;
SenseHubV2& SenseHubV2::operator=(SenseHubV2&&) noexcept = default;
SenseHubV2::~SenseHubV2() noexcept = default;

// The stub load.  sense_hub_v2_implemented is false, and the header gives
// the load a CRUCIBLE_STUB deprecation.  The hub stays unavailable until
// the loader opens the v2 object.
::fixy::Option<SenseHubV2> load_sense_hub_v2(::fixy::InitLoadCtx const&) noexcept { return ::fixy::none; }

v2::CounterSnapshot SenseHubV2::read_counters() const noexcept { return v2::CounterSnapshot{}; }

v2::GaugeSnapshot SenseHubV2::read_gauges() const noexcept { return v2::GaugeSnapshot{}; }

::fixy::Borrowed<const volatile uint64_t, SenseHubV2> SenseHubV2::counters_view() const noexcept {
    return ::fixy::Borrowed<const volatile uint64_t, SenseHubV2>{};
}

::fixy::Borrowed<const volatile uint64_t, SenseHubV2> SenseHubV2::gauges_view() const noexcept {
    return ::fixy::Borrowed<const volatile uint64_t, SenseHubV2>{};
}

v2::LoadReport SenseHubV2::coverage() const noexcept { return v2::LoadReport{}; }

}  // namespace crucible::perf
