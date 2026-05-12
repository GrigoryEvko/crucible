#pragma once

// GAPS-113.  Deterministic per-Cog health substrate.
//
// This header intentionally evaluates already-harvested telemetry facts.  It
// does not read NVML, EDAC, sysfs, TCP_INFO, or Scuttlebutt directly; those
// harvest and transport surfaces are owned by later GAPS.  The load-bearing
// invariant here is that every consumer sees one bounded, typed health score
// instead of inventing its own timeout / thermal / ECC / drop-rate policy.

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

enum class HealthState : std::uint8_t {
    Healthy = 0,
    Suspect = 1,
    Quarantined = 2,
    Recovered = 3,
    Permanent = 4,
};

[[nodiscard]] std::string_view health_state_name(HealthState state) noexcept;

enum class HealthIssue : std::uint32_t {
    PhiSuspect         = 1u << 0,
    PhiQuarantine     = 1u << 1,
    ThermalWarn       = 1u << 2,
    ThermalCritical   = 1u << 3,
    ClockDegraded     = 1u << 4,
    CorrectedEccTrend = 1u << 5,
    UncorrectedEcc    = 1u << 6,
    DropRateWarn      = 1u << 7,
    DropRateCritical  = 1u << 8,
    WearWarn          = 1u << 9,
    WearCritical      = 1u << 10,
    MissingSample     = 1u << 11,
};

[[nodiscard]] std::string_view health_issue_name(HealthIssue issue) noexcept;

struct Health_Degraded : safety::diag::tag_base {
    static constexpr std::string_view name = "Health_Degraded";
    static constexpr std::string_view description =
        "A Cog's composite health score crossed a configured risk threshold.";
    static constexpr std::string_view remediation =
        "Route new work away from the Cog, preserve the transition event for "
        "postmortem, and let the later quarantine policy decide whether to "
        "isolate the Cog.";
};

using PositiveNanoseconds = safety::Positive<std::uint64_t>;

class [[nodiscard]] PhiMilli {
    std::uint32_t value_ = 0;

public:
    constexpr PhiMilli() noexcept = default;
    explicit constexpr PhiMilli(std::uint32_t milli_phi) noexcept
        : value_{milli_phi} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr double value() const noexcept {
        return static_cast<double>(value_) / 1000.0;
    }

    constexpr auto operator<=>(PhiMilli const&) const noexcept = default;
};

static_assert(sizeof(PhiMilli) == sizeof(std::uint32_t));

class [[nodiscard]] HealthScore {
    std::uint16_t value_ = 1000;

public:
    constexpr HealthScore() noexcept = default;
    explicit constexpr HealthScore(std::uint16_t value) noexcept
        : value_{value > 1000u ? std::uint16_t{1000} : value} {}

    [[nodiscard]] static constexpr HealthScore perfect() noexcept {
        return HealthScore{1000};
    }

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
    PositiveNanoseconds expected_heartbeat_ns{std::uint64_t{1'000'000'000}};
    std::int32_t thermal_warn_millicelsius = 80'000;
    std::int32_t thermal_critical_millicelsius = 90'000;
    std::uint8_t clock_degraded_pct = 10;
    std::uint32_t corrected_ecc_warn_delta = 10;
    std::uint32_t drop_warn_ppm = 1'000;
    std::uint32_t drop_critical_ppm = 10'000;
    std::uint32_t wear_warn_ppm = 800'000;
    std::uint32_t wear_critical_ppm = 950'000;
};

struct ThermalSample {
    std::int32_t temperature_millicelsius = 0;
    std::uint8_t clock_degraded_pct = 0;
    std::uint64_t sequence = 0;
};

struct EccCounters {
    safety::Monotonic<std::uint64_t> corrected{0};
    safety::Monotonic<std::uint64_t> uncorrected{0};
    std::uint64_t sequence = 0;
};

struct DropCounters {
    safety::Monotonic<std::uint64_t> rx_packets{0};
    safety::Monotonic<std::uint64_t> tx_packets{0};
    safety::Monotonic<std::uint64_t> rx_dropped{0};
    safety::Monotonic<std::uint64_t> tx_dropped{0};
    safety::Monotonic<std::uint64_t> rx_fifo_errors{0};
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
    safety::Bits<HealthIssue> issues{};
    std::uint32_t drop_rate_ppm = 0;
    std::uint32_t wear_used_ppm = 0;
    std::uint64_t sequence = 0;
};

struct HealthDeltaEvent {
    cog::Uuid cog_uuid{};
    HealthState from = HealthState::Healthy;
    HealthState to = HealthState::Healthy;
    HealthScore score{};
    safety::Bits<HealthIssue> issues{};
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsHealthMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsHealthUpdate =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Bg>;

namespace detail {

[[nodiscard]] constexpr bool same_uuid(cog::Uuid lhs, cog::Uuid rhs) noexcept {
    return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
}

[[nodiscard]] constexpr std::uint32_t clamp_u32(std::uint64_t value,
                                                std::uint32_t hi) noexcept {
    return value > hi ? hi : static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t scale_ppm(std::uint64_t numerator,
                                                std::uint64_t denominator) noexcept {
    if (denominator == 0 || numerator == 0) {
        return 0;
    }
    std::uint64_t const quotient = numerator / denominator;
    std::uint64_t const remainder = numerator % denominator;
    constexpr std::uint64_t kScale = 1'000'000;
    if (quotient > std::numeric_limits<std::uint32_t>::max() / kScale) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    std::uint64_t const scaled_quotient = quotient * kScale;
    std::uint64_t const scaled_remainder =
        remainder > std::numeric_limits<std::uint64_t>::max() / kScale
            ? std::numeric_limits<std::uint64_t>::max()
            : (remainder * kScale) / denominator;
    if (scaled_quotient > std::numeric_limits<std::uint32_t>::max()
        || scaled_remainder > std::numeric_limits<std::uint32_t>::max() - scaled_quotient) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(scaled_quotient + scaled_remainder);
}

[[nodiscard]] constexpr std::uint16_t normalized_weight(HealthWeights weights) noexcept {
    std::uint32_t const sum = static_cast<std::uint32_t>(weights.phi)
        + weights.thermal + weights.ecc + weights.drop + weights.wear;
    if (sum == 0) {
        return std::uint16_t{1};
    }
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(sum, UINT16_MAX));
}

[[nodiscard]] constexpr std::uint32_t risk_component(std::uint32_t risk,
                                                     std::uint16_t weight) noexcept {
    std::uint64_t const weighted = static_cast<std::uint64_t>(
        std::min(risk, 1000u)) * weight;
    return clamp_u32(weighted, std::numeric_limits<std::uint32_t>::max());
}

}  // namespace detail

template <std::size_t MaxPeers, std::size_t Window = 32>
class PhiAccrualDetector : safety::Pinned<PhiAccrualDetector<MaxPeers, Window>> {
    static_assert(MaxPeers > 0, "PhiAccrualDetector requires at least one peer slot");
    static_assert(Window >= 2, "PhiAccrualDetector needs at least two heartbeat intervals");

    struct Slot {
        bool occupied = false;
        cog::CogIdentity peer{};
        std::array<std::uint64_t, Window> intervals{};
        std::uint16_t count = 0;
        std::uint16_t next = 0;
        safety::Monotonic<std::uint64_t> last_heartbeat_ns{0};
        safety::Monotonic<std::uint64_t> sequence{0};
    };

    std::array<Slot, MaxPeers> slots_{};
    HealthPolicy policy_{};

    [[nodiscard]] constexpr Slot* find_or_insert(cog::CogIdentity const& peer) noexcept {
        for (auto& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.peer.uuid, peer.uuid)) {
                return &slot;
            }
        }
        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.peer = peer;
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr Slot const* find(cog::CogIdentity const& peer) const noexcept {
        for (auto const& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.peer.uuid, peer.uuid)) {
                return &slot;
            }
        }
        return nullptr;
    }

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
        std::uint64_t mean = static_cast<std::uint64_t>(total / slot.count);
        return mean == 0 ? std::uint64_t{1} : mean;
    }

public:
    explicit constexpr PhiAccrualDetector(HealthPolicy policy = {}) noexcept
        : policy_{policy} {}

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool record_heartbeat(Ctx const&,
                                                  cog::CogIdentity const& peer,
                                                  std::uint64_t observed_ns,
                                                  std::uint64_t sequence = 0) noexcept {
        Slot* slot = find_or_insert(peer);
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

    [[nodiscard]] PhiMilli suspicion_phi(cog::CogIdentity const& peer,
                                         std::uint64_t now_ns) const noexcept {
        Slot const* slot = find(peer);
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
        double const milli = std::min(phi * 1000.0, 1'000'000.0);
        return PhiMilli{static_cast<std::uint32_t>(milli)};
    }
};

template <std::size_t MaxPeers, std::size_t Window = 32, std::size_t MaxEvents = MaxPeers * 4>
class CompositeHealthScorer
    : safety::Pinned<CompositeHealthScorer<MaxPeers, Window, MaxEvents>> {
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
        safety::Monotonic<std::uint64_t> sequence{0};
        safety::Monotonic<std::uint64_t> transition_count{0};
    };

    std::array<Slot, MaxPeers> slots_{};
    std::array<HealthDeltaEvent, MaxEvents> events_{};
    std::uint16_t next_event_ = 0;
    std::uint16_t event_count_ = 0;
    HealthPolicy policy_{};
    PhiAccrualDetector<MaxPeers, Window> phi_;

    [[nodiscard]] constexpr Slot* find_or_insert(cog::CogIdentity const& peer) noexcept {
        for (auto& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.peer.uuid, peer.uuid)) {
                return &slot;
            }
        }
        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.peer = peer;
                slot.last_snapshot.cog_uuid = peer.uuid;
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr Slot const* find(cog::CogIdentity const& peer) const noexcept {
        for (auto const& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.peer.uuid, peer.uuid)) {
                return &slot;
            }
        }
        return nullptr;
    }

    constexpr void append_event(Slot& slot,
                                HealthState from,
                                HealthState to,
                                HealthSnapshot const& snapshot) noexcept {
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
                                                       safety::Bits<HealthIssue>& issues) const noexcept {
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
            auto const delta = static_cast<std::uint32_t>(
                slot.thermal.temperature_millicelsius - policy_.thermal_warn_millicelsius);
            auto const span = static_cast<std::uint32_t>(
                std::max(1, policy_.thermal_critical_millicelsius
                            - policy_.thermal_warn_millicelsius));
            risk = std::min(1000u, 500u + detail::clamp_u32(
                (static_cast<std::uint64_t>(delta) * 500u) / span, 500u));
        }
        if (slot.thermal.clock_degraded_pct >= policy_.clock_degraded_pct) {
            issues.set(HealthIssue::ClockDegraded);
            risk = std::max(risk, static_cast<std::uint32_t>(
                std::min(1000u, static_cast<std::uint32_t>(
                    slot.thermal.clock_degraded_pct) * 10u)));
        }
        return risk;
    }

    [[nodiscard]] constexpr std::uint32_t ecc_risk(Slot const& slot,
                                                   safety::Bits<HealthIssue>& issues) const noexcept {
        if (!slot.has_ecc) {
            issues.set(HealthIssue::MissingSample);
            return 0;
        }
        std::uint64_t const uncorrected = slot.ecc.uncorrected.get();
        if (uncorrected > slot.prior_ecc.uncorrected.get()) {
            issues.set(HealthIssue::UncorrectedEcc);
            return 1000;
        }
        std::uint64_t const corrected_delta =
            slot.ecc.corrected.get() - slot.prior_ecc.corrected.get();
        if (corrected_delta >= policy_.corrected_ecc_warn_delta) {
            issues.set(HealthIssue::CorrectedEccTrend);
            return std::min(1000u, static_cast<std::uint32_t>(
                400u + corrected_delta * 10u));
        }
        return 0;
    }

    [[nodiscard]] constexpr std::uint32_t drop_risk(Slot const& slot,
                                                    safety::Bits<HealthIssue>& issues,
                                                    std::uint32_t& out_ppm) const noexcept {
        if (!slot.has_drops) {
            issues.set(HealthIssue::MissingSample);
            out_ppm = 0;
            return 0;
        }
        std::uint64_t const packets =
            (slot.drops.rx_packets.get() - slot.prior_drops.rx_packets.get())
          + (slot.drops.tx_packets.get() - slot.prior_drops.tx_packets.get());
        std::uint64_t const dropped =
            (slot.drops.rx_dropped.get() - slot.prior_drops.rx_dropped.get())
          + (slot.drops.tx_dropped.get() - slot.prior_drops.tx_dropped.get())
          + (slot.drops.rx_fifo_errors.get() - slot.prior_drops.rx_fifo_errors.get());
        out_ppm = detail::scale_ppm(dropped, packets);
        if (out_ppm >= policy_.drop_critical_ppm) {
            issues.set(HealthIssue::DropRateCritical);
            return 1000;
        }
        if (out_ppm >= policy_.drop_warn_ppm) {
            issues.set(HealthIssue::DropRateWarn);
            std::uint32_t const span = std::max(1u,
                policy_.drop_critical_ppm - policy_.drop_warn_ppm);
            return std::min(1000u, 400u + ((out_ppm - policy_.drop_warn_ppm) * 600u) / span);
        }
        return 0;
    }

    [[nodiscard]] constexpr std::uint32_t wear_risk(Slot const& slot,
                                                    safety::Bits<HealthIssue>& issues) const noexcept {
        if (!slot.has_wear) {
            return 0;
        }
        if (slot.wear.used_ppm >= policy_.wear_critical_ppm) {
            issues.set(HealthIssue::WearCritical);
            return 1000;
        }
        if (slot.wear.used_ppm >= policy_.wear_warn_ppm) {
            issues.set(HealthIssue::WearWarn);
            std::uint32_t const span = std::max(1u,
                policy_.wear_critical_ppm - policy_.wear_warn_ppm);
            return std::min(1000u, 300u + ((slot.wear.used_ppm - policy_.wear_warn_ppm) * 700u) / span);
        }
        return 0;
    }

    [[nodiscard]] constexpr HealthState next_state(HealthState current,
                                                   HealthSnapshot const& snapshot) const noexcept {
        if (current == HealthState::Permanent) {
            return HealthState::Permanent;
        }
        if (snapshot.issues.test(HealthIssue::UncorrectedEcc)
            || snapshot.issues.test(HealthIssue::ThermalCritical)
            || snapshot.issues.test(HealthIssue::WearCritical)) {
            return HealthState::Permanent;
        }
        if (snapshot.phi.raw() >= policy_.quarantine_phi.raw()
            || snapshot.score.raw() < policy_.quarantine_below.raw()) {
            return HealthState::Quarantined;
        }
        if (snapshot.phi.raw() >= policy_.suspect_phi.raw()
            || snapshot.score.raw() < policy_.suspect_below.raw()) {
            return HealthState::Suspect;
        }
        if ((current == HealthState::Suspect || current == HealthState::Quarantined)
            && snapshot.score.raw() >= policy_.recovered_at_or_above.raw()) {
            return HealthState::Recovered;
        }
        return HealthState::Healthy;
    }

public:
    explicit constexpr CompositeHealthScorer(HealthPolicy policy = {}) noexcept
        : policy_{policy}, phi_{policy} {}

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool record_heartbeat(Ctx const& ctx,
                                                  cog::CogIdentity const& peer,
                                                  std::uint64_t observed_ns,
                                                  std::uint64_t sequence = 0) noexcept {
        return phi_.record_heartbeat(ctx, peer, observed_ns, sequence)
            && find_or_insert(peer) != nullptr;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_thermal(Ctx const&,
                                                cog::CogIdentity const& peer,
                                                ThermalSample sample) noexcept {
        Slot* slot = find_or_insert(peer);
        if (slot == nullptr) {
            return false;
        }
        if (!slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->thermal = sample;
        slot->has_thermal = true;
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_ecc(Ctx const&,
                                            cog::CogIdentity const& peer,
                                            EccCounters sample) noexcept {
        Slot* slot = find_or_insert(peer);
        if (slot == nullptr) {
            return false;
        }
        if (sample.corrected.get() < slot->ecc.corrected.get()
            || sample.uncorrected.get() < slot->ecc.uncorrected.get()) {
            return false;
        }
        if (!slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->prior_ecc = slot->ecc;
        slot->ecc = sample;
        slot->has_ecc = true;
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_drops(Ctx const&,
                                              cog::CogIdentity const& peer,
                                              DropCounters sample) noexcept {
        Slot* slot = find_or_insert(peer);
        if (slot == nullptr) {
            return false;
        }
        if (sample.rx_packets.get() < slot->drops.rx_packets.get()
            || sample.tx_packets.get() < slot->drops.tx_packets.get()
            || sample.rx_dropped.get() < slot->drops.rx_dropped.get()
            || sample.tx_dropped.get() < slot->drops.tx_dropped.get()
            || sample.rx_fifo_errors.get() < slot->drops.rx_fifo_errors.get()) {
            return false;
        }
        if (!slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->prior_drops = slot->drops;
        slot->drops = sample;
        slot->has_drops = true;
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsHealthUpdate<Ctx>
    [[nodiscard]] constexpr bool update_wear(Ctx const&,
                                             cog::CogIdentity const& peer,
                                             WearSample sample) noexcept {
        if (sample.used_ppm > 1'000'000u) {
            return false;
        }
        Slot* slot = find_or_insert(peer);
        if (slot == nullptr || !slot->sequence.try_advance(sample.sequence)) {
            return false;
        }
        slot->wear = sample;
        slot->has_wear = true;
        return true;
    }

    [[nodiscard]] constexpr safety::Stale<HealthSnapshot>
    compute(cog::CogIdentity const& peer,
            std::uint64_t now_ns,
            std::uint64_t sequence) noexcept {
        Slot* slot = find_or_insert(peer);
        if (slot == nullptr) {
            HealthSnapshot missing{};
            missing.cog_uuid = peer.uuid;
            missing.issues.set(HealthIssue::MissingSample);
            missing.score = HealthScore{0};
            missing.state = HealthState::Quarantined;
            missing.sequence = sequence;
            return safety::Stale<HealthSnapshot>::at(missing, 1);
        }

        safety::Bits<HealthIssue> issues{};
        PhiMilli const phi = phi_.suspicion_phi(peer, now_ns);
        if (phi.raw() >= policy_.quarantine_phi.raw()) {
            issues.set(HealthIssue::PhiQuarantine);
        } else if (phi.raw() >= policy_.suspect_phi.raw()) {
            issues.set(HealthIssue::PhiSuspect);
        }

        std::uint32_t drop_ppm = 0;
        std::uint32_t const phi_risk = std::min(1000u,
            (phi.raw() * 1000u) / std::max(1u, policy_.quarantine_phi.raw()));
        std::uint32_t const thermal = thermal_risk(*slot, issues);
        std::uint32_t const ecc = ecc_risk(*slot, issues);
        std::uint32_t const drop = drop_risk(*slot, issues, drop_ppm);
        std::uint32_t const wear = wear_risk(*slot, issues);

        std::uint32_t const weighted =
            detail::risk_component(phi_risk, policy_.weights.phi)
          + detail::risk_component(thermal, policy_.weights.thermal)
          + detail::risk_component(ecc, policy_.weights.ecc)
          + detail::risk_component(drop, policy_.weights.drop)
          + detail::risk_component(wear, policy_.weights.wear);
        std::uint32_t const risk = weighted / detail::normalized_weight(policy_.weights);
        HealthScore const score{static_cast<std::uint16_t>(
            1000u - std::min(risk, 1000u))};

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

        std::uint64_t staleness = 0;
        if (sequence >= slot->sequence.get()) {
            staleness = sequence - slot->sequence.get();
        }
        return safety::Stale<HealthSnapshot>::at(snapshot, staleness);
    }

    [[nodiscard]] constexpr safety::Stale<HealthSnapshot>
    current(cog::CogIdentity const& peer, std::uint64_t sequence) const noexcept {
        Slot const* slot = find(peer);
        if (slot == nullptr) {
            HealthSnapshot missing{};
            missing.cog_uuid = peer.uuid;
            missing.state = HealthState::Quarantined;
            missing.score = HealthScore{0};
            missing.issues.set(HealthIssue::MissingSample);
            missing.sequence = sequence;
            return safety::Stale<HealthSnapshot>::at(missing, 1);
        }
        std::uint64_t staleness = 0;
        if (sequence >= slot->sequence.get()) {
            staleness = sequence - slot->sequence.get();
        }
        return safety::Stale<HealthSnapshot>::at(slot->last_snapshot, staleness);
    }

    [[nodiscard]] constexpr std::span<const HealthDeltaEvent>
    transition_events() const noexcept {
        return std::span<const HealthDeltaEvent>{events_.data(), event_count_};
    }

    [[nodiscard]] constexpr std::uint16_t transition_event_count() const noexcept {
        return event_count_;
    }
};

template <effects::IsExecCtx Ctx, std::size_t MaxPeers, std::size_t Window = 32, std::size_t MaxEvents = MaxPeers * 4>
    requires CtxFitsHealthMint<Ctx>
[[nodiscard]] constexpr CompositeHealthScorer<MaxPeers, Window, MaxEvents>
mint_topology_health(Ctx const&, HealthPolicy policy = {}) noexcept {
    return CompositeHealthScorer<MaxPeers, Window, MaxEvents>{policy};
}

static_assert(safety::diag::is_diagnostic_class_v<Health_Degraded>);
static_assert(std::is_trivially_copyable_v<HealthSnapshot>);
static_assert(std::is_trivially_destructible_v<HealthSnapshot>);
static_assert(sizeof(HealthDeltaEvent) <= 64);
static_assert(CtxFitsHealthMint<effects::ColdInitCtx>);
static_assert(!CtxFitsHealthMint<effects::BgDrainCtx>);
static_assert(CtxFitsHealthUpdate<effects::BgDrainCtx>);
static_assert(!CtxFitsHealthUpdate<effects::HotFgCtx>);

}  // namespace crucible::topology
