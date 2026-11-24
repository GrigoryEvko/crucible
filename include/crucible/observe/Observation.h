#pragma once

// These values are measured facts, not proofs. The compiler cannot bound
// them. Deterministic replay and safety contracts must not consume them as
// evidence.

#include <fixy/Core.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Pinned.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
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

// The cells of the observations of one source, and their one writer.
//
// Each cell is a snapshot (fixy/concurrent/AtomicSnapshot.h): readers on other
// threads load it while the writer publishes.  The writer end is the one writer
// of each cell, so the cells name it as the owner of the proof of their writer,
// and a publish takes no locked step.  The door writer() is the one door to a
// writer end.  It takes the claim of the writer role first, so a second live
// writer end ends the process at that door, not at a publish.  The end gives
// the claim back when it ends.  A move clears its binding, so a moved-from end
// gives no claim back and publishes nothing.  The end refers to its sinks
// through the binding, so the borrow rules of the quarantine plugin refuse an
// end that a function gives back or stores past the life of its sinks.
//
// The writer end and the readers use the cells on their threads at the same
// time by design.  Each member is private: the cells are snapshots, and the
// claim is an atomic cell.  So the class states synchronized{}
// (foundation/Transfer.h).
template <std::size_t Count>
class[[= ::foundation::transfer::synchronized{}]] ObservationSinks
    : public ::foundation::Pinned<ObservationSinks<Count>> {
public:
    class Writer;

private:
    // The role of the publish of the writer end in the proof of the one
    // writer of each cell.
    struct publish_role_ final {};
    using cell_type =
        ::fixy::concurrent::AtomicSnapshot<Observation, ::foundation::SoleWriterProof<Writer, publish_role_>>;

public:
    // The writer end of the cells.  Only the door writer() makes one.
    class Writer {
        ::foundation::ChannelBinding<ObservationSinks> sinks_;

        Writer(ObservationSinks& sinks, ::foundation::BindingKey<ObservationSinks> key) noexcept : sinks_{sinks, key} {}
        friend class ObservationSinks;

    public:
        Writer(Writer const&) = delete("the writer end of a set of observation sinks is its one writer");
        Writer& operator=(Writer const&) = delete("the writer end of a set of observation sinks is its one writer");
        Writer(Writer&&) noexcept = default;
        Writer& operator=(Writer&&) = delete("a writer end binds to one set of observation sinks for its life");
        ~Writer() {
            if (sinks_.is_bound()) sinks_->writer_claim_.release();
        }

        // Publishes observation into the cell at index.
        void record(::fixy::Index<Count> index, Observation const& observation) noexcept {
            ::foundation::SoleWriterProof<Writer, publish_role_> const proof{};
            sinks_->cells_.with_element(
                index, [&proof, &observation](cell_type& cell) noexcept { cell.publish(proof, observation); });
        }
    };

    ObservationSinks() noexcept = default;

    // The one writer end.  A second live end ends the process here.
    [[nodiscard]] Writer writer() noexcept {
        writer_claim_.take("two live writer ends of one set of observation sinks");
        return Writer{*this, ::foundation::BindingKey<ObservationSinks>{}};
    }

    // The last observation of the cell at index.  The read spins with no exit
    // while a write is in flight, so the context owns Div (AtomicSnapshot::load).
    template <class Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
    [[nodiscard]] Observation load(Ctx const& ctx, ::fixy::Index<Count> index) const noexcept {
        return cells_.at(index).load(ctx);
    }

private:
    ::fixy::FixedArray<cell_type, Count> cells_{};
    ::foundation::EndpointClaim writer_claim_;
};

// The cell of a source with one observation.
using ObservationSink = ObservationSinks<1>;

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

}  // namespace crucible::observe
