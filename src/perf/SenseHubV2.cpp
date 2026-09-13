#include <crucible/perf/SenseHubV2.h>
#include <crucible/perf/ProcGauges.h>

#include <utility>

namespace crucible::perf {

struct SenseHubV2::State {
    int placeholder = 0;
};

SenseHubV2::SenseHubV2(std::unique_ptr<State> s) noexcept : state_{std::move(s)} {}

SenseHubV2::SenseHubV2(SenseHubV2&&) noexcept = default;
SenseHubV2& SenseHubV2::operator=(SenseHubV2&&) noexcept = default;
SenseHubV2::~SenseHubV2() noexcept = default;

std::optional<SenseHubV2> SenseHubV2::load(::crucible::effects::Init) noexcept { return std::nullopt; }

CounterSnapshot SenseHubV2::read_counters() const noexcept { return CounterSnapshot{}; }

GaugeSnapshot SenseHubV2::read_gauges() const noexcept { return GaugeSnapshot{}; }

safety::Borrowed<const volatile uint64_t, SenseHubV2> SenseHubV2::counters_view() const noexcept {
    return {nullptr, 0};
}

safety::Borrowed<const volatile uint64_t, SenseHubV2> SenseHubV2::gauges_view() const noexcept { return {nullptr, 0}; }

LoadReport SenseHubV2::coverage() const noexcept { return LoadReport{}; }

}  // namespace crucible::perf
