#pragma once

// These values are measured facts, not proofs. The compiler cannot bound
// them. Deterministic replay and safety contracts must not consume them as
// evidence.

#include <fixy/concurrent/AtomicSnapshot.h>

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

using ObservationSnapshot = ::fixy::concurrent::AtomicSnapshot<Observation>;

// The family of a packed metric id: the kind of the source that owns a range
// of ids.  The value of each family is an ASCII letter.
enum class MetricFamily : std::uint8_t {
    Congestion = 0x43,
    TopologyHealth = 0x48,
};

// The layout of a packed metric id, from the high bit: the family in 8 bits,
// the index of the source in 16 bits, and the slot of the metric in 8 bits.
// The three widths fill the 32 bits of the id.
inline constexpr unsigned kMetricFamilyBits = 8;
inline constexpr unsigned kMetricIndexBits = 16;
inline constexpr unsigned kMetricSlotBits = 8;

// The packed metric id of one slot of the source at index.  Each parameter
// has the width of its field, so no input loses a bit, and two ids are equal
// only when their three fields are equal.
[[nodiscard]] constexpr std::uint32_t pack_metric_id(MetricFamily family, std::uint16_t index,
                                                     std::uint8_t slot) noexcept {
    return (static_cast<std::uint32_t>(family) << (kMetricIndexBits + kMetricSlotBits))
         | (static_cast<std::uint32_t>(index) << kMetricSlotBits) | static_cast<std::uint32_t>(slot);
}

[[nodiscard]] constexpr Observation make_observation(ObservationKind kind, ObservationSource source,
                                                     std::uint32_t metric_id, std::uint64_t value,
                                                     std::uint64_t sequence = 0) noexcept {
    return Observation{
        .kind = kind,
        .source = source,
        .reserved = 0,
        .metric_id = metric_id,
        .value = value,
        .sequence = sequence,
    };
}

[[nodiscard]] constexpr Observation latency_ns(std::uint32_t metric_id, std::uint64_t value, std::uint64_t sequence = 0,
                                               ObservationSource source = ObservationSource::Runtime) noexcept {
    return make_observation(ObservationKind::LatencyNs, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation energy_pj(std::uint32_t metric_id, std::uint64_t value, std::uint64_t sequence = 0,
                                              ObservationSource source = ObservationSource::Runtime) noexcept {
    return make_observation(ObservationKind::EnergyPj, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation power_mw(std::uint32_t metric_id, std::uint64_t value, std::uint64_t sequence = 0,
                                             ObservationSource source = ObservationSource::Runtime) noexcept {
    return make_observation(ObservationKind::PowerMw, source, metric_id, value, sequence);
}

[[nodiscard]] constexpr Observation bits_transferred(std::uint32_t metric_id, std::uint64_t value,
                                                     std::uint64_t sequence = 0,
                                                     ObservationSource source = ObservationSource::Runtime) noexcept {
    return make_observation(ObservationKind::BitsTransferred, source, metric_id, value, sequence);
}

inline void record_observation(ObservationSnapshot& sink, Observation observation) noexcept {
    sink.publish(observation);
}

// The read spins with no exit while a write is in flight, so the context owns
// Div (AtomicSnapshot::load).
template <class Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
[[nodiscard]] inline Observation latest_observation(Ctx const& ctx, ObservationSnapshot const& sink) noexcept {
    return sink.load(ctx);
}

}  // namespace crucible::observe
