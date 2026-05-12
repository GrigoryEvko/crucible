#pragma once

// GAPS-118. Bounded runtime quarantine policy for Cogs.
//
// This header consumes already-scored health snapshots, asymmetric-failure
// classifications, and recovery probe outcomes. It does not evict from gossip,
// mutate routing tables, schedule live probes, or write Cipher records. Those
// side effects are owned by later Canopy/CNTP/Cipher tasks. The invariant here
// is a typed hysteretic action substrate with explicit operator authority for
// permanent removal.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/observe/SyntheticProbe.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/topology/AsymmetricFailure.h>
#include <crucible/topology/Health.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::warden {

enum class QuarantineState : std::uint8_t {
    Healthy = 0,
    Suspect = 1,
    Quarantined = 2,
    Recovered = 3,
    Permanent = 4,
};

[[nodiscard]] std::string_view quarantine_state_name(QuarantineState state) noexcept;

enum class QuarantineSignal : std::uint32_t {
    HealthSuspect = 1u << 0,
    HealthQuarantine = 1u << 1,
    CriticalHealthIssue = 1u << 2,
    AsymmetricSuspect = 1u << 3,
    AsymmetricDead = 1u << 4,
    RecoveryProbePassed = 1u << 5,
    RecoveryProbeFailed = 1u << 6,
    RecoveryThresholdMet = 1u << 7,
    PermanentTimerElapsed = 1u << 8,
    PermanentRequiresOperator = 1u << 9,
    OperatorOverride = 1u << 10,
};

[[nodiscard]] std::string_view quarantine_signal_name(QuarantineSignal signal) noexcept;

struct QuarantineTransition : safety::diag::tag_base {
    static constexpr std::string_view name = "QuarantineTransition";
    static constexpr std::string_view description =
        "A Cog changed quarantine/routing-admission state.";
    static constexpr std::string_view remediation =
        "Preserve the transition event, stop routing new work to Quarantined "
        "or Permanent Cogs, and require operator authority for Permanent.";
};

namespace quarantine_tag {
struct OperatorOverride {};
}  // namespace quarantine_tag

using PositiveRecoveryProbeCount = safety::Positive<std::uint16_t>;
using PositiveNanoseconds = safety::Positive<std::uint64_t>;

struct QuarantineConfig {
    topology::HealthScore suspect_at_or_below{900};
    topology::HealthScore quarantine_at_or_below{500};
    PositiveRecoveryProbeCount recovery_probe_count{std::uint16_t{100}};
    PositiveNanoseconds permanent_after_ns{std::uint64_t{3'600'000'000'000ull}};
    std::uint32_t canary_load_ppm = 10'000;
};

struct QuarantineSnapshot {
    cog::Uuid cog_uuid{};
    QuarantineState state = QuarantineState::Healthy;
    topology::HealthScore health_score{};
    safety::Bits<QuarantineSignal> signals{};
    std::uint16_t consecutive_recovery_probes = 0;
    std::uint32_t admitted_load_ppm = 1'000'000;
    std::uint64_t quarantine_since_ns = 0;
    std::uint64_t sequence = 0;
};

struct QuarantineEvent {
    cog::Uuid cog_uuid{};
    QuarantineState from = QuarantineState::Healthy;
    QuarantineState to = QuarantineState::Healthy;
    topology::HealthScore health_score{};
    safety::Bits<QuarantineSignal> signals{};
    std::uint16_t consecutive_recovery_probes = 0;
    std::uint32_t admitted_load_ppm = 1'000'000;
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsQuarantineMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsQuarantineRecord =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Bg>;

template <class Ctx>
concept CtxFitsQuarantineOverride =
       effects::IsExecCtx<Ctx>
    && (effects::row_contains_v<effects::row_type_of_t<Ctx>,
                                effects::Effect::Init>
        || effects::row_contains_v<effects::row_type_of_t<Ctx>,
                                   effects::Effect::Test>);

namespace detail {

[[nodiscard]] constexpr bool same_uuid(cog::Uuid lhs, cog::Uuid rhs) noexcept {
    return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
}

[[nodiscard]] constexpr std::uint32_t load_for(QuarantineState state,
                                               QuarantineConfig const& config) noexcept {
    switch (state) {
        case QuarantineState::Healthy:     return 1'000'000;
        case QuarantineState::Suspect:     return 300'000;
        case QuarantineState::Quarantined: return 0;
        case QuarantineState::Recovered:   return config.canary_load_ppm;
        case QuarantineState::Permanent:   return 0;
        default:                           return 0;
    }
}

[[nodiscard]] constexpr bool critical_health(topology::HealthSnapshot const& snapshot) noexcept {
    return snapshot.issues.test(topology::HealthIssue::UncorrectedEcc)
        || snapshot.issues.test(topology::HealthIssue::ThermalCritical)
        || snapshot.issues.test(topology::HealthIssue::WearCritical);
}

}  // namespace detail

template <std::size_t MaxCogs, std::size_t MaxEvents = MaxCogs * 4>
class QuarantinePolicy : public safety::Pinned<QuarantinePolicy<MaxCogs, MaxEvents>> {
    static_assert(MaxCogs > 0, "QuarantinePolicy requires Cog slots");
    static_assert(MaxEvents > 0, "QuarantinePolicy requires audit-event slots");

    struct Slot {
        bool occupied = false;
        cog::Uuid cog_uuid{};
        QuarantineState state = QuarantineState::Healthy;
        topology::HealthScore health_score{};
        safety::Bits<QuarantineSignal> signals{};
        std::uint16_t consecutive_recovery_probes = 0;
        std::uint64_t quarantine_since_ns = 0;
        safety::Monotonic<std::uint64_t> sequence{0};
    };

    std::array<Slot, MaxCogs> slots_{};
    std::array<QuarantineEvent, MaxEvents> events_{};
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    QuarantineConfig config_{};

    [[nodiscard]] constexpr Slot* find_or_insert(cog::CogIdentity const& id) noexcept {
        if (id.uuid.is_zero()) {
            return nullptr;
        }
        for (auto& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.cog_uuid, id.uuid)) {
                return &slot;
            }
        }
        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.cog_uuid = id.uuid;
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr Slot const* find(cog::CogIdentity const& id) const noexcept {
        for (auto const& slot : slots_) {
            if (slot.occupied && detail::same_uuid(slot.cog_uuid, id.uuid)) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr QuarantineSnapshot snapshot(Slot const& slot) const noexcept {
        return QuarantineSnapshot{
            .cog_uuid = slot.cog_uuid,
            .state = slot.state,
            .health_score = slot.health_score,
            .signals = slot.signals,
            .consecutive_recovery_probes = slot.consecutive_recovery_probes,
            .admitted_load_ppm = detail::load_for(slot.state, config_),
            .quarantine_since_ns = slot.quarantine_since_ns,
            .sequence = slot.sequence.get(),
        };
    }

    constexpr void append_event(Slot const& slot,
                                QuarantineState from,
                                QuarantineState to) noexcept {
        events_[next_event_] = QuarantineEvent{
            .cog_uuid = slot.cog_uuid,
            .from = from,
            .to = to,
            .health_score = slot.health_score,
            .signals = slot.signals,
            .consecutive_recovery_probes = slot.consecutive_recovery_probes,
            .admitted_load_ppm = detail::load_for(to, config_),
            .sequence = slot.sequence.get(),
        };
        next_event_ = (next_event_ + 1u) % events_.size();
        if (event_count_ < events_.size()) {
            ++event_count_;
        }
    }

    constexpr void transition_to(Slot& slot,
                                 QuarantineState next,
                                 std::uint64_t now_ns) noexcept {
        if (slot.state == next) {
            return;
        }
        QuarantineState const prior = slot.state;
        slot.state = next;
        if (next == QuarantineState::Quarantined && slot.quarantine_since_ns == 0) {
            slot.quarantine_since_ns = now_ns;
        }
        if (next == QuarantineState::Healthy || next == QuarantineState::Suspect) {
            slot.quarantine_since_ns = 0;
            slot.consecutive_recovery_probes = 0;
        }
        append_event(slot, prior, next);
    }

    [[nodiscard]] constexpr QuarantineState state_from_health(
        Slot const& slot,
        topology::HealthSnapshot const& health,
        safety::Bits<QuarantineSignal>& signals) const noexcept {
        if (slot.state == QuarantineState::Permanent) {
            return QuarantineState::Permanent;
        }
        if (detail::critical_health(health)) {
            signals.set(QuarantineSignal::CriticalHealthIssue);
            return QuarantineState::Quarantined;
        }
        if (health.state == topology::HealthState::Quarantined
            || health.score.raw() <= config_.quarantine_at_or_below.raw()) {
            signals.set(QuarantineSignal::HealthQuarantine);
            return QuarantineState::Quarantined;
        }
        if (health.state == topology::HealthState::Suspect
            || health.score.raw() <= config_.suspect_at_or_below.raw()) {
            signals.set(QuarantineSignal::HealthSuspect);
            return QuarantineState::Suspect;
        }
        if (slot.state == QuarantineState::Recovered) {
            return QuarantineState::Healthy;
        }
        if (slot.state == QuarantineState::Quarantined) {
            return QuarantineState::Quarantined;
        }
        return QuarantineState::Healthy;
    }

public:
    explicit constexpr QuarantinePolicy(QuarantineConfig config = {}) noexcept
        : config_{config} {}

    [[nodiscard]] constexpr QuarantineConfig config() const noexcept {
        return config_;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool on_health_event(Ctx const&,
                                                 cog::CogIdentity const& cog,
                                                 topology::HealthSnapshot health,
                                                 std::uint64_t now_ns) noexcept {
        Slot* slot = find_or_insert(cog);
        if (slot == nullptr || !slot->sequence.try_advance(health.sequence)) {
            return false;
        }
        slot->health_score = health.score;
        slot->signals = {};
        QuarantineState const next = state_from_health(*slot, health, slot->signals);
        transition_to(*slot, next, now_ns);
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool on_asymmetric_failure(
        Ctx const&,
        cog::CogIdentity const& cog,
        topology::FailureClass cls,
        std::uint64_t now_ns,
        std::uint64_t sequence) noexcept {
        Slot* slot = find_or_insert(cog);
        if (slot == nullptr || !slot->sequence.try_advance(sequence)) {
            return false;
        }
        slot->signals = {};
        QuarantineState next = slot->state;
        if (cls == topology::FailureClass::BidiFailed) {
            slot->signals.set(QuarantineSignal::AsymmetricDead);
            next = QuarantineState::Quarantined;
        } else if (cls == topology::FailureClass::TxBroken
                   || cls == topology::FailureClass::RxBroken) {
            slot->signals.set(QuarantineSignal::AsymmetricSuspect);
            if (slot->state == QuarantineState::Healthy
                || slot->state == QuarantineState::Recovered) {
                next = QuarantineState::Suspect;
            }
        }
        transition_to(*slot, next, now_ns);
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool record_recovery_probe(
        Ctx const&,
        cog::CogIdentity const& cog,
        observe::ProbeOutcome outcome,
        std::uint64_t now_ns) noexcept {
        Slot* slot = find_or_insert(cog);
        if (slot == nullptr || !slot->sequence.try_advance(outcome.sequence)) {
            return false;
        }
        slot->signals = {};
        if (slot->state != QuarantineState::Quarantined) {
            return true;
        }
        if (outcome.ok()) {
            slot->signals.set(QuarantineSignal::RecoveryProbePassed);
            if (slot->consecutive_recovery_probes
                < config_.recovery_probe_count.value()) {
                ++slot->consecutive_recovery_probes;
            }
            if (slot->consecutive_recovery_probes
                >= config_.recovery_probe_count.value()) {
                slot->signals.set(QuarantineSignal::RecoveryThresholdMet);
                transition_to(*slot, QuarantineState::Recovered, now_ns);
            }
        } else {
            slot->signals.set(QuarantineSignal::RecoveryProbeFailed);
            slot->consecutive_recovery_probes = 0;
        }
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool check_permanent_deadline(Ctx const&,
                                                          cog::CogIdentity const& cog,
                                                          std::uint64_t now_ns,
                                                          std::uint64_t sequence) noexcept {
        Slot* slot = find_or_insert(cog);
        if (slot == nullptr || !slot->sequence.try_advance(sequence)) {
            return false;
        }
        slot->signals = {};
        if (slot->state != QuarantineState::Quarantined
            || slot->quarantine_since_ns == 0
            || now_ns < slot->quarantine_since_ns
            || now_ns - slot->quarantine_since_ns < config_.permanent_after_ns.value()) {
            return true;
        }
        slot->signals.set(QuarantineSignal::PermanentTimerElapsed);
        slot->signals.set(QuarantineSignal::PermanentRequiresOperator);
        append_event(*slot, slot->state, slot->state);
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineOverride<Ctx>
    [[nodiscard]] constexpr safety::Permission<quarantine_tag::OperatorOverride>
    operator_override(Ctx const&,
                      safety::Permission<quarantine_tag::OperatorOverride>&& authority,
                      cog::CogIdentity const& cog,
                      QuarantineState forced,
                      std::uint64_t now_ns,
                      std::uint64_t sequence) noexcept {
        Slot* slot = find_or_insert(cog);
        if (slot != nullptr && slot->sequence.try_advance(sequence)) {
            slot->signals = {};
            slot->signals.set(QuarantineSignal::OperatorOverride);
            transition_to(*slot, forced, now_ns);
        }
        return std::move(authority);
    }

    [[nodiscard]] constexpr QuarantineState state(cog::CogIdentity const& id) const noexcept {
        Slot const* slot = find(id);
        if (slot == nullptr) {
            return QuarantineState::Healthy;
        }
        return slot->state;
    }

    [[nodiscard]] constexpr QuarantineSnapshot current(cog::CogIdentity const& id) const noexcept {
        Slot const* slot = find(id);
        if (slot == nullptr) {
            QuarantineSnapshot missing{};
            missing.cog_uuid = id.uuid;
            return missing;
        }
        return snapshot(*slot);
    }

    [[nodiscard]] constexpr std::span<const QuarantineEvent>
    transition_events() const noexcept {
        return std::span<const QuarantineEvent>{events_.data(), event_count_};
    }

    [[nodiscard]] constexpr std::size_t transition_event_count() const noexcept {
        return event_count_;
    }
};

template <effects::IsExecCtx Ctx, std::size_t MaxCogs, std::size_t MaxEvents = MaxCogs * 4>
    requires CtxFitsQuarantineMint<Ctx>
[[nodiscard]] constexpr QuarantinePolicy<MaxCogs, MaxEvents>
mint_quarantine_policy(Ctx const&, QuarantineConfig config = {}) noexcept {
    return QuarantinePolicy<MaxCogs, MaxEvents>{config};
}

static_assert(safety::diag::is_diagnostic_class_v<QuarantineTransition>);
static_assert(std::is_trivially_copyable_v<QuarantineSnapshot>);
static_assert(std::is_trivially_copyable_v<QuarantineEvent>);
static_assert(sizeof(QuarantineEvent) <= 64);
static_assert(CtxFitsQuarantineMint<effects::ColdInitCtx>);
static_assert(!CtxFitsQuarantineMint<effects::BgDrainCtx>);
static_assert(CtxFitsQuarantineRecord<effects::BgDrainCtx>);
static_assert(!CtxFitsQuarantineRecord<effects::HotFgCtx>);
static_assert(CtxFitsQuarantineOverride<effects::ColdInitCtx>);
static_assert(CtxFitsQuarantineOverride<effects::TestRunnerCtx>);
static_assert(!CtxFitsQuarantineOverride<effects::BgDrainCtx>);

}  // namespace crucible::warden
