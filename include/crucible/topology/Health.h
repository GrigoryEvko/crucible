#pragma once

#include <crucible/cog/CogIdentity.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Instance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

// foundation::reflect::enum_name gives the log spelling of both enums.
enum class HealthState : std::uint8_t {
    Healthy = 0,
    Suspect = 1,
    Quarantined = 2,
    Recovered = 3,
    Permanent = 4,
};

enum class HealthIssue : std::uint32_t {
    PhiSuspect = 1u << 0,
    PhiQuarantine = 1u << 1,
    ThermalWarn = 1u << 2,
    ThermalCritical = 1u << 3,
    ClockDegraded = 1u << 4,
    CorrectedEccTrend = 1u << 5,
    UncorrectedEcc = 1u << 6,
    DropRateWarn = 1u << 7,
    DropRateCritical = 1u << 8,
    WearWarn = 1u << 9,
    WearCritical = 1u << 10,
    MissingSample = 1u << 11,
};

struct Health_Degraded : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "Health_Degraded";
    static constexpr std::string_view description =
        "A Cog's composite health score crossed a configured risk threshold.";
    static constexpr std::string_view remediation =
        "Route new work away from the Cog, preserve the transition event for "
        "postmortem, and let the later quarantine policy decide whether to "
        "isolate the Cog.";
};

using PositiveNanoseconds = ::fixy::Positive<std::uint64_t>;

class [[nodiscard]] PhiMilli {
    std::uint32_t value_ = 0;

public:
    constexpr PhiMilli() noexcept = default;
    explicit constexpr PhiMilli(std::uint32_t milli_phi) noexcept : value_{milli_phi} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr double value() const noexcept { return static_cast<double>(value_) / 1000.0; }

    constexpr auto operator<=>(PhiMilli const&) const noexcept = default;
};

static_assert(sizeof(PhiMilli) == sizeof(std::uint32_t));

class [[nodiscard]] HealthScore {
    std::uint16_t value_ = 1000;

public:
    constexpr HealthScore() noexcept = default;
    explicit constexpr HealthScore(std::uint16_t value) noexcept
        : value_{value > 1000u ? std::uint16_t{1000} : value} {}

    [[nodiscard]] static constexpr HealthScore perfect() noexcept { return HealthScore{1000}; }

    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return value_; }

    constexpr auto operator<=>(HealthScore const&) const noexcept = default;
};

static_assert(sizeof(HealthScore) == sizeof(std::uint16_t));

struct HealthWeights {
    std::uint16_t phi = 350;
    std::uint16_t thermal = 180;
    std::uint16_t ecc = 220;
    std::uint16_t drop = 180;
    std::uint16_t wear = 70;
};

struct HealthPolicy {
    HealthWeights weights{};
    PhiMilli suspect_phi{4000};
    PhiMilli quarantine_phi{8000};
    HealthScore suspect_below{750};
    HealthScore quarantine_below{400};
    HealthScore recovered_at_or_above{900};
    PositiveNanoseconds expected_heartbeat_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000000000});
    std::int32_t thermal_warn_millicelsius = 80000;
    std::int32_t thermal_critical_millicelsius = 90000;
    std::uint8_t clock_degraded_pct = 10;
    std::uint32_t corrected_ecc_warn_delta = 10;
    std::uint32_t drop_warn_ppm = 1000;
    std::uint32_t drop_critical_ppm = 10000;
    std::uint32_t wear_warn_ppm = 800000;
    std::uint32_t wear_critical_ppm = 950000;
};

struct ThermalSample {
    std::int32_t temperature_millicelsius = 0;
    std::uint8_t clock_degraded_pct = 0;
    std::uint64_t sequence = 0;
};

using MonotoneCount = ::fixy::Monotonic<std::uint64_t>;

struct EccCounters {
    MonotoneCount corrected = ::fixy::mint_monotonic<std::uint64_t>(0);
    MonotoneCount uncorrected = ::fixy::mint_monotonic<std::uint64_t>(0);
    std::uint64_t sequence = 0;
};

struct DropCounters {
    MonotoneCount rx_packets = ::fixy::mint_monotonic<std::uint64_t>(0);
    MonotoneCount tx_packets = ::fixy::mint_monotonic<std::uint64_t>(0);
    MonotoneCount rx_dropped = ::fixy::mint_monotonic<std::uint64_t>(0);
    MonotoneCount tx_dropped = ::fixy::mint_monotonic<std::uint64_t>(0);
    MonotoneCount rx_fifo_errors = ::fixy::mint_monotonic<std::uint64_t>(0);
    std::uint64_t sequence = 0;
};

struct WearSample {
    std::uint32_t used_ppm = 0;
    std::uint64_t sequence = 0;
};

struct HealthSnapshot {
    cog::Uuid cog_uuid{};
    HealthState state = HealthState::Healthy;
    HealthScore score{};
    PhiMilli phi{};
    ::fixy::Bits<HealthIssue> issues{};
    std::uint32_t drop_rate_ppm = 0;
    std::uint32_t wear_used_ppm = 0;
    std::uint64_t sequence = 0;
};

struct HealthDeltaEvent {
    cog::Uuid cog_uuid{};
    HealthState from = HealthState::Healthy;
    HealthState to = HealthState::Healthy;
    HealthScore score{};
    ::fixy::Bits<HealthIssue> issues{};
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsHealthMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsHealthUpdate =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

namespace detail {

[[nodiscard]] constexpr std::uint32_t clamp_u32(std::uint64_t value, std::uint32_t hi) noexcept {
    return value > hi ? hi : static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t scale_ppm(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0 || numerator == 0) {
        return 0;
    }
    std::uint64_t const quotient = numerator / denominator;
    std::uint64_t const remainder = numerator % denominator;
    constexpr std::uint64_t kScale = 1000000;
    if (quotient > std::numeric_limits<std::uint32_t>::max() / kScale) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    std::uint64_t const scaled_quotient = quotient * kScale;
    std::uint64_t const scaled_remainder = remainder > std::numeric_limits<std::uint64_t>::max() / kScale
                                             ? std::numeric_limits<std::uint64_t>::max()
                                             : (remainder * kScale) / denominator;
    if (scaled_quotient > std::numeric_limits<std::uint32_t>::max()
        || scaled_remainder > std::numeric_limits<std::uint32_t>::max() - scaled_quotient) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(scaled_quotient + scaled_remainder);
}

[[nodiscard]] constexpr std::uint16_t normalized_weight(HealthWeights weights) noexcept {
    std::uint32_t const sum =
        static_cast<std::uint32_t>(weights.phi) + weights.thermal + weights.ecc + weights.drop + weights.wear;
    if (sum == 0) {
        return std::uint16_t{1};
    }
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(sum, UINT16_MAX));
}

[[nodiscard]] constexpr std::uint32_t risk_component(std::uint32_t risk, std::uint16_t weight) noexcept {
    std::uint64_t const weighted = static_cast<std::uint64_t>(std::min(risk, 1000u)) * weight;
    return clamp_u32(weighted, std::numeric_limits<std::uint32_t>::max());
}

// The occupied slot that holds this peer, or nullptr.  One lookup serves
// the detector and the scorer, and the const and the mutable callers.
template <class Slots>
[[nodiscard]] constexpr auto find_peer_slot(Slots& slots, cog::Uuid const& uuid) noexcept
    -> decltype(std::addressof(*std::begin(slots))) {
    for (auto& slot : slots) {
        if (slot.occupied && slot.peer.uuid == uuid) {
            return std::addressof(slot);
        }
    }
    return nullptr;
}

// The slot of this peer, claimed from a free slot on first sight, or
// nullptr when every slot holds another peer.
template <class Slots>
[[nodiscard]] constexpr auto claim_peer_slot(Slots& slots, cog::CogIdentity const& peer) noexcept
    -> decltype(std::addressof(*std::begin(slots))) {
    if (auto* found = find_peer_slot(slots, peer.uuid)) {
        return found;
    }
    for (auto& slot : slots) {
        if (!slot.occupied) {
            slot.occupied = true;
            slot.peer = peer;
            return std::addressof(slot);
        }
    }
    return nullptr;
}

template <class T>
concept IsMonotoneCount = ::foundation::reflect::IsInstanceOf<T, ^^::fixy::Monotonic>;

// The number of monotone counters in a counter struct.  A counter struct
// with none would make no_counter_behind vacuously true, so it is refused.
template <class Counters>
[[nodiscard]] consteval std::size_t monotone_count_members() noexcept {
    std::size_t count = 0;
    for (std::meta::info member :
         std::meta::nonstatic_data_members_of(^^Counters, std::meta::access_context::current())) {
        if (std::meta::extract<bool>(std::meta::substitute(^^IsMonotoneCount, {std::meta::type_of(member)}))) {
            ++count;
        }
    }
    return count;
}

// True when `sample` is a monotone counter behind its peer `stored`.  A
// member of another type is not a counter, and is never behind.
template <class Member>
[[nodiscard]] constexpr bool is_counter_behind(Member const& sample, Member const& stored) noexcept {
    if constexpr (IsMonotoneCount<Member>) {
        typename Member::comparator_type const goes_backward{};
        return goes_backward(sample.get(), stored.get());
    } else {
        return false;
    }
}

// True when no monotone counter in `sample` is behind its peer in
// `stored`.  The structured bindings take every member of the struct, so a
// counter added to the struct is checked with no second list to keep in step.
template <class Counters>
[[nodiscard]] constexpr bool no_counter_behind(Counters const& sample, Counters const& stored) noexcept {
    static_assert(monotone_count_members<Counters>() > 0,
                  "no_counter_behind needs a struct with at least one monotone counter");
    auto const& [... sample_members] = sample;
    auto const& [... stored_members] = stored;
    return !(is_counter_behind(sample_members, stored_members) || ...);
}

// Moves `stored` to its peer `sample`.  A monotone counter advances,
// which a Monotonic admits only forward, and any other member is copied.
template <class Member>
constexpr void take_member(Member& stored, Member const& sample) noexcept {
    if constexpr (IsMonotoneCount<Member>) {
        stored.advance(sample.get());
    } else {
        stored = sample;
    }
}

// Moves every member of `stored` to its peer in `sample`.  The caller
// proves no_counter_behind(sample, stored) first, so each counter advance
// holds its precondition.  The structured bindings take every member, as
// in no_counter_behind.
template <class Counters>
constexpr void take_counters(Counters& stored, Counters const& sample) noexcept {
    auto& [... stored_members] = stored;
    auto const& [... sample_members] = sample;
    (take_member(stored_members, sample_members), ...);
}

// Internal to the scorer: it owns the only instance, and the scorer's
// mint is the only way to reach one.
template <std::size_t MaxPeers, std::size_t Window>
class PhiAccrualDetector : ::foundation::Pinned<PhiAccrualDetector<MaxPeers, Window>> {
    static_assert(MaxPeers > 0, "PhiAccrualDetector requires at least one peer slot");
    static_assert(Window >= 2, "PhiAccrualDetector needs at least two heartbeat intervals");

    struct Slot {
        bool occupied = false;
        cog::CogIdentity peer{};
        std::array<std::uint64_t, Window> intervals{};
        std::uint16_t count = 0;
        std::uint16_t next = 0;
        MonotoneCount last_heartbeat_ns = ::fixy::mint_monotonic<std::uint64_t>(0);
        MonotoneCount sequence = ::fixy::mint_monotonic<std::uint64_t>(0);
    };

    std::array<Slot, MaxPeers> slots_{};
    HealthPolicy policy_{};

    [[nodiscard]] constexpr std::uint64_t mean_interval_ns(Slot const& slot) const noexcept {
        if (slot.count == 0) {
            return policy_.expected_heartbeat_ns.value();
        }
        std::uint64_t total = 0;
        for (std::uint16_t i = 0; i < slot.count; ++i) {
            if (slot.intervals[i] > std::numeric_limits<std::uint64_t>::max() - total) {
                total = std::numeric_limits<std::uint64_t>::max();
            } else {
                total += slot.intervals[i];
            }
        }
        std::uint64_t const mean = total / slot.count;
        return mean == 0 ? std::uint64_t{1} : mean;
    }

public:
    explicit constexpr PhiAccrualDetector(HealthPolicy policy) noexcept : policy_{policy} {}

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool record_heartbeat(Ctx const&, cog::CogIdentity const& peer, std::uint64_t observed_ns,
                                                  std::uint64_t sequence) noexcept {
        Slot* slot = claim_peer_slot(slots_, peer);
        if (slot == nullptr) {
            return false;
        }
        std::uint64_t const prior = slot->last_heartbeat_ns.get();
        if (observed_ns < prior) {
            return false;
        }
        if (prior != 0 && observed_ns > prior) {
            slot->intervals[slot->next] = observed_ns - prior;
            slot->next = static_cast<std::uint16_t>((slot->next + 1u) % Window);
            if (slot->count < Window) {
                ++slot->count;
            }
        }
        slot->last_heartbeat_ns.advance(observed_ns);
        (void)slot->sequence.try_advance(sequence);
        return true;
    }

    [[nodiscard]] PhiMilli suspicion_phi(cog::CogIdentity const& peer, std::uint64_t now_ns) const noexcept {
        Slot const* slot = find_peer_slot(slots_, peer.uuid);
        if (slot == nullptr || slot->last_heartbeat_ns.get() == 0) {
            return PhiMilli{0};
        }
        std::uint64_t const last = slot->last_heartbeat_ns.get();
        if (now_ns <= last) {
            return PhiMilli{0};
        }

        double const delay = static_cast<double>(now_ns - last);
        double const mean = static_cast<double>(mean_interval_ns(*slot));
        double const phi = delay / (mean * std::log(10.0));
        if (!(phi > 0.0)) {
            return PhiMilli{0};
        }
        double const milli = std::min(phi * 1000.0, 1000000.0);
        return PhiMilli{static_cast<std::uint32_t>(milli)};
    }
};

}  // namespace detail

template <std::size_t MaxPeers, std::size_t Window, std::size_t MaxEvents>
class CompositeHealthScorer;

// The only door into a scorer.  An Init-row context mints it; a
// background worker then feeds it samples.
template <class Ctx, std::size_t MaxPeers, std::size_t Window = 32, std::size_t MaxEvents = MaxPeers * 4>
    requires CtxFitsHealthMint<Ctx>
[[nodiscard]] constexpr CompositeHealthScorer<MaxPeers, Window, MaxEvents>
mint_topology_health(Ctx const&, HealthPolicy policy = {}) noexcept;

template <std::size_t MaxPeers, std::size_t Window, std::size_t MaxEvents>
class CompositeHealthScorer : ::foundation::Pinned<CompositeHealthScorer<MaxPeers, Window, MaxEvents>> {
    static_assert(MaxEvents > 0, "CompositeHealthScorer needs an event ring");

    struct Slot {
        bool occupied = false;
        cog::CogIdentity peer{};
        HealthState state = HealthState::Healthy;
        HealthSnapshot last_snapshot{};
        ThermalSample thermal{};
        EccCounters ecc{};
        EccCounters prior_ecc{};
        DropCounters drops{};
        DropCounters prior_drops{};
        WearSample wear{};
        bool has_thermal = false;
        bool has_ecc = false;
        bool has_drops = false;
        bool has_wear = false;
        MonotoneCount sequence = ::fixy::mint_monotonic<std::uint64_t>(0);
        MonotoneCount transition_count = ::fixy::mint_monotonic<std::uint64_t>(0);
    };

    std::array<Slot, MaxPeers> slots_{};
    std::array<HealthDeltaEvent, MaxEvents> events_{};
    std::uint16_t next_event_ = 0;
    std::uint16_t event_count_ = 0;
    HealthPolicy policy_{};
    detail::PhiAccrualDetector<MaxPeers, Window> phi_;

    template <class Ctx, std::size_t Peers, std::size_t W, std::size_t Events>
        requires CtxFitsHealthMint<Ctx>
    friend constexpr CompositeHealthScorer<Peers, W, Events> mint_topology_health(Ctx const&,
                                                                                  HealthPolicy policy) noexcept;

    explicit constexpr CompositeHealthScorer(HealthPolicy policy) noexcept : policy_{policy}, phi_{policy} {}

    [[nodiscard]] constexpr Slot* claim(cog::CogIdentity const& peer) noexcept {
        Slot* slot = detail::claim_peer_slot(slots_, peer);
        if (slot != nullptr) {
            slot->last_snapshot.cog_uuid = peer.uuid;
        }
        return slot;
    }

    // The snapshot for a peer the scorer has no slot for: quarantined,
    // scored zero and flagged as missing, one step stale.
    [[nodiscard]] static constexpr ::fixy::Stale<HealthSnapshot> missing_snapshot(cog::CogIdentity const& peer,
                                                                                  std::uint64_t sequence) noexcept {
        HealthSnapshot missing{};
        missing.cog_uuid = peer.uuid;
        missing.state = HealthState::Quarantined;
        missing.score = HealthScore{0};
        missing.issues.set(HealthIssue::MissingSample);
        missing.sequence = sequence;
        return ::fixy::Stale<HealthSnapshot>::at(missing, 1);
    }

    // How many sequence steps the slot's last sample lags `sequence`.
    [[nodiscard]] static constexpr std::uint64_t lag_behind(Slot const& slot, std::uint64_t sequence) noexcept {
        return sequence >= slot.sequence.get() ? sequence - slot.sequence.get() : 0;
    }

    constexpr void append_event(Slot& slot, HealthState from, HealthState to, HealthSnapshot const& snapshot) noexcept {
        events_[next_event_] = HealthDeltaEvent{
            .cog_uuid = slot.peer.uuid,
            .from = from,
            .to = to,
            .score = snapshot.score,
            .issues = snapshot.issues,
            .sequence = snapshot.sequence,
        };
        next_event_ = static_cast<std::uint16_t>((next_event_ + 1u) % MaxEvents);
        if (event_count_ < MaxEvents) {
            ++event_count_;
        }
        slot.transition_count.bump();
    }

    [[nodiscard]] constexpr std::uint32_t thermal_risk(Slot const& slot,
                                                       ::fixy::Bits<HealthIssue>& issues) const noexcept {
        if (!slot.has_thermal) {
            issues.set(HealthIssue::MissingSample);
            return 0;
        }
        std::uint32_t risk = 0;
        if (slot.thermal.temperature_millicelsius >= policy_.thermal_critical_millicelsius) {
            issues.set(HealthIssue::ThermalCritical);
            risk = 1000;
        } else if (slot.thermal.temperature_millicelsius >= policy_.thermal_warn_millicelsius) {
            issues.set(HealthIssue::ThermalWarn);
            auto const delta =
                static_cast<std::uint32_t>(slot.thermal.temperature_millicelsius - policy_.thermal_warn_millicelsius);
            auto const span = static_cast<std::uint32_t>(
                std::max(1, policy_.thermal_critical_millicelsius - policy_.thermal_warn_millicelsius));
            risk = std::min(1000u, 500u + detail::clamp_u32((static_cast<std::uint64_t>(delta) * 500u) / span, 500u));
        }
        if (slot.thermal.clock_degraded_pct >= policy_.clock_degraded_pct) {
            issues.set(HealthIssue::ClockDegraded);
            risk = std::max(risk, static_cast<std::uint32_t>(std::min(
                                      1000u, static_cast<std::uint32_t>(slot.thermal.clock_degraded_pct) * 10u)));
        }
        return risk;
    }

    [[nodiscard]] constexpr std::uint32_t ecc_risk(Slot const& slot, ::fixy::Bits<HealthIssue>& issues) const noexcept {
        if (!slot.has_ecc) {
            issues.set(HealthIssue::MissingSample);
            return 0;
        }
        std::uint64_t const uncorrected = slot.ecc.uncorrected.get();
        if (uncorrected > slot.prior_ecc.uncorrected.get()) {
            issues.set(HealthIssue::UncorrectedEcc);
            return 1000;
        }
        std::uint64_t const corrected_delta = slot.ecc.corrected.get() - slot.prior_ecc.corrected.get();
        if (corrected_delta >= policy_.corrected_ecc_warn_delta) {
            issues.set(HealthIssue::CorrectedEccTrend);
            return std::min(1000u, static_cast<std::uint32_t>(400u + corrected_delta * 10u));
        }
        return 0;
    }

    [[nodiscard]] constexpr std::uint32_t drop_risk(Slot const& slot, ::fixy::Bits<HealthIssue>& issues,
                                                    std::uint32_t& out_ppm) const noexcept {
        if (!slot.has_drops) {
            issues.set(HealthIssue::MissingSample);
            out_ppm = 0;
            return 0;
        }
        std::uint64_t const packets = (slot.drops.rx_packets.get() - slot.prior_drops.rx_packets.get())
                                    + (slot.drops.tx_packets.get() - slot.prior_drops.tx_packets.get());
        std::uint64_t const dropped = (slot.drops.rx_dropped.get() - slot.prior_drops.rx_dropped.get())
                                    + (slot.drops.tx_dropped.get() - slot.prior_drops.tx_dropped.get())
                                    + (slot.drops.rx_fifo_errors.get() - slot.prior_drops.rx_fifo_errors.get());
        out_ppm = detail::scale_ppm(dropped, packets);
        if (out_ppm >= policy_.drop_critical_ppm) {
            issues.set(HealthIssue::DropRateCritical);
            return 1000;
        }
        if (out_ppm >= policy_.drop_warn_ppm) {
            issues.set(HealthIssue::DropRateWarn);
            std::uint32_t const span = std::max(1u, policy_.drop_critical_ppm - policy_.drop_warn_ppm);
            return std::min(1000u, 400u + ((out_ppm - policy_.drop_warn_ppm) * 600u) / span);
        }
        return 0;
    }

    [[nodiscard]] constexpr std::uint32_t wear_risk(Slot const& slot,
                                                    ::fixy::Bits<HealthIssue>& issues) const noexcept {
        if (!slot.has_wear) {
            return 0;
        }
        if (slot.wear.used_ppm >= policy_.wear_critical_ppm) {
            issues.set(HealthIssue::WearCritical);
            return 1000;
        }
        if (slot.wear.used_ppm >= policy_.wear_warn_ppm) {
            issues.set(HealthIssue::WearWarn);
            std::uint32_t const span = std::max(1u, policy_.wear_critical_ppm - policy_.wear_warn_ppm);
            return std::min(1000u, 300u + ((slot.wear.used_ppm - policy_.wear_warn_ppm) * 700u) / span);
        }
        return 0;
    }

    [[nodiscard]] constexpr HealthState next_state(HealthState current, HealthSnapshot const& snapshot) const noexcept {
        if (current == HealthState::Permanent) {
            return HealthState::Permanent;
        }
        if (snapshot.issues.test(HealthIssue::UncorrectedEcc) || snapshot.issues.test(HealthIssue::ThermalCritical)
            || snapshot.issues.test(HealthIssue::WearCritical)) {
            return HealthState::Permanent;
        }
        if (snapshot.phi.raw() >= policy_.quarantine_phi.raw()
            || snapshot.score.raw() < policy_.quarantine_below.raw()) {
            return HealthState::Quarantined;
        }
        if (snapshot.phi.raw() >= policy_.suspect_phi.raw() || snapshot.score.raw() < policy_.suspect_below.raw()) {
            return HealthState::Suspect;
        }
        if ((current == HealthState::Suspect || current == HealthState::Quarantined)
            && snapshot.score.raw() >= policy_.recovered_at_or_above.raw()) {
            return HealthState::Recovered;
        }
        return HealthState::Healthy;
    }

public:
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool record_heartbeat(Ctx const& ctx, cog::CogIdentity const& peer,
                                                  std::uint64_t observed_ns, std::uint64_t sequence = 0) noexcept {
        return phi_.record_heartbeat(ctx, peer, observed_ns, sequence) && claim(peer) != nullptr;
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_thermal(Ctx const&, cog::CogIdentity const& peer,
                                                ThermalSample sample) noexcept {
        Slot* slot = claim(peer);
        if (slot == nullptr || !slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->thermal = sample;
        slot->has_thermal = true;
        return true;
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_ecc(Ctx const&, cog::CogIdentity const& peer, EccCounters sample) noexcept {
        Slot* slot = claim(peer);
        if (slot == nullptr || !detail::no_counter_behind(sample, slot->ecc)
            || !slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        // The prior sample trails the current one, so both only advance.
        detail::take_counters(slot->prior_ecc, slot->ecc);
        detail::take_counters(slot->ecc, sample);
        slot->has_ecc = true;
        return true;
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_drops(Ctx const&, cog::CogIdentity const& peer, DropCounters sample) noexcept {
        Slot* slot = claim(peer);
        if (slot == nullptr || !detail::no_counter_behind(sample, slot->drops)
            || !slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        detail::take_counters(slot->prior_drops, slot->drops);
        detail::take_counters(slot->drops, sample);
        slot->has_drops = true;
        return true;
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_wear(Ctx const&, cog::CogIdentity const& peer, WearSample sample) noexcept {
        if (sample.used_ppm > 1000000u) {
            return false;
        }
        Slot* slot = claim(peer);
        if (slot == nullptr || !slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->wear = sample;
        slot->has_wear = true;
        return true;
    }

    [[nodiscard]] constexpr ::fixy::Stale<HealthSnapshot> compute(cog::CogIdentity const& peer, std::uint64_t now_ns,
                                                                  std::uint64_t sequence) noexcept {
        Slot* slot = claim(peer);
        if (slot == nullptr) {
            return missing_snapshot(peer, sequence);
        }

        ::fixy::Bits<HealthIssue> issues{};
        PhiMilli const phi = phi_.suspicion_phi(peer, now_ns);
        if (phi.raw() >= policy_.quarantine_phi.raw()) {
            issues.set(HealthIssue::PhiQuarantine);
        } else if (phi.raw() >= policy_.suspect_phi.raw()) {
            issues.set(HealthIssue::PhiSuspect);
        }

        std::uint32_t drop_ppm = 0;
        std::uint32_t const phi_risk =
            std::min(1000u, (phi.raw() * 1000u) / std::max(1u, policy_.quarantine_phi.raw()));
        std::uint32_t const thermal = thermal_risk(*slot, issues);
        std::uint32_t const ecc = ecc_risk(*slot, issues);
        std::uint32_t const drop = drop_risk(*slot, issues, drop_ppm);
        std::uint32_t const wear = wear_risk(*slot, issues);

        std::uint32_t const weighted = detail::risk_component(phi_risk, policy_.weights.phi)
                                     + detail::risk_component(thermal, policy_.weights.thermal)
                                     + detail::risk_component(ecc, policy_.weights.ecc)
                                     + detail::risk_component(drop, policy_.weights.drop)
                                     + detail::risk_component(wear, policy_.weights.wear);
        std::uint32_t const risk = weighted / detail::normalized_weight(policy_.weights);
        HealthScore const score{static_cast<std::uint16_t>(1000u - std::min(risk, 1000u))};

        HealthSnapshot snapshot{
            .cog_uuid = peer.uuid,
            .state = slot->state,
            .score = score,
            .phi = phi,
            .issues = issues,
            .drop_rate_ppm = drop_ppm,
            .wear_used_ppm = slot->wear.used_ppm,
            .sequence = sequence,
        };
        HealthState const new_state = next_state(slot->state, snapshot);
        snapshot.state = new_state;
        if (new_state != slot->state) {
            append_event(*slot, slot->state, new_state, snapshot);
            slot->state = new_state;
        }
        slot->last_snapshot = snapshot;
        (void)slot->sequence.try_advance(sequence);
        return ::fixy::Stale<HealthSnapshot>::at(snapshot, lag_behind(*slot, sequence));
    }

    [[nodiscard]] constexpr ::fixy::Stale<HealthSnapshot> current(cog::CogIdentity const& peer,
                                                                  std::uint64_t sequence) const noexcept {
        Slot const* slot = detail::find_peer_slot(slots_, peer.uuid);
        if (slot == nullptr) {
            return missing_snapshot(peer, sequence);
        }
        return ::fixy::Stale<HealthSnapshot>::at(slot->last_snapshot, lag_behind(*slot, sequence));
    }

    [[nodiscard]] constexpr std::span<const HealthDeltaEvent> transition_events() const noexcept {
        return std::span<const HealthDeltaEvent>{events_.data(), event_count_};
    }

    [[nodiscard]] constexpr std::uint16_t transition_event_count() const noexcept { return event_count_; }
};

template <class Ctx, std::size_t MaxPeers, std::size_t Window, std::size_t MaxEvents>
    requires CtxFitsHealthMint<Ctx>
[[nodiscard]] constexpr CompositeHealthScorer<MaxPeers, Window, MaxEvents>
mint_topology_health(Ctx const&, HealthPolicy policy) noexcept {
    return CompositeHealthScorer<MaxPeers, Window, MaxEvents>{policy};
}

static_assert(::foundation::diag::is_diagnostic_class_v<Health_Degraded>);
static_assert(std::is_trivially_copyable_v<HealthSnapshot>);
static_assert(std::is_trivially_destructible_v<HealthSnapshot>);
static_assert(sizeof(HealthDeltaEvent) <= 64);
static_assert(detail::monotone_count_members<EccCounters>() == 2);
static_assert(detail::monotone_count_members<DropCounters>() == 5);
static_assert(!std::is_constructible_v<CompositeHealthScorer<1, 2, 1>, HealthPolicy>,
              "the scorer is reached only through mint_topology_health");
static_assert(CtxFitsHealthMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsHealthMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsHealthUpdate<::fixy::BgDrainCtx>);
static_assert(!CtxFitsHealthUpdate<::fixy::HotFgCtx>);

}  // namespace crucible::topology
