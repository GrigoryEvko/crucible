#pragma once

// Observation rail for physical signals.
//
// Wall-clock latency, energy, power, and transferred-bit counts are operational
// facts, not type-system guarantees.  The compiler cannot prove physical
// bounds, so these values deliberately live in observe/ as latest-value runtime
// observations.  Deterministic replay and safety contracts must not consume
// this header as proof evidence.

#include <crucible/concurrent/AtomicSnapshot.h>

#include <cstdint>
#include <type_traits>

namespace crucible::observe {

enum class ObservationKind : std::uint8_t {
    Metric = 0,
    LatencyNs = 1,
    EnergyPj = 2,
    PowerMw = 3,
    WallClockNs = 4,
    BitsTransferred = 5,
    HealthScore = 6,
    PhiMilli = 7,
    DropRatePpm = 8,
    WearUsedPpm = 9,
};

enum class ObservationSource : std::uint8_t {
    Runtime = 0,
    Bpf = 1,
    Watchdog = 2,
    Keeper = 3,
    Operator = 4,
};

struct Observation {
    ObservationKind kind = ObservationKind::Metric;
    ObservationSource source = ObservationSource::Runtime;
    std::uint16_t reserved = 0;
    std::uint32_t metric_id = 0;
    std::uint64_t value = 0;
    std::uint64_t sequence = 0;
};

static_assert(std::is_trivially_copyable_v<Observation>);
static_assert(std::is_trivially_destructible_v<Observation>);

using ObservationSnapshot = ::crucible::concurrent::AtomicSnapshot<Observation>;

[[nodiscard]] constexpr Observation
make_observation(
    ObservationKind kind,
    ObservationSource source,
    std::uint32_t metric_id,
    std::uint64_t value,
    std::uint64_t sequence = 0) noexcept
{
    return Observation{
        .kind = kind,
        .source = source,
        .reserved = 0,
        .metric_id = metric_id,
        .value = value,
        .sequence = sequence,
    };
}

[[nodiscard]] constexpr Observation
latency_ns(
    std::uint32_t metric_id,
    std::uint64_t value,
    std::uint64_t sequence = 0,
    ObservationSource source = ObservationSource::Runtime) noexcept
{
    return make_observation(
        ObservationKind::LatencyNs, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation
energy_pj(
    std::uint32_t metric_id,
    std::uint64_t value,
    std::uint64_t sequence = 0,
    ObservationSource source = ObservationSource::Runtime) noexcept
{
    return make_observation(
        ObservationKind::EnergyPj, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation
power_mw(
    std::uint32_t metric_id,
    std::uint64_t value,
    std::uint64_t sequence = 0,
    ObservationSource source = ObservationSource::Runtime) noexcept
{
    return make_observation(
        ObservationKind::PowerMw, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation
bits_transferred(
    std::uint32_t metric_id,
    std::uint64_t value,
    std::uint64_t sequence = 0,
    ObservationSource source = ObservationSource::Runtime) noexcept
{
    return make_observation(
        ObservationKind::BitsTransferred, source, metric_id, value, sequence);
}

inline void
record_observation(ObservationSnapshot& sink, Observation observation) noexcept {
    sink.publish(observation);
}

[[nodiscard]] inline Observation
latest_observation(ObservationSnapshot const& sink) noexcept {
    return sink.load();
}

}  // namespace crucible::observe
