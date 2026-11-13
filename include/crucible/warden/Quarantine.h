#pragma once

// The policy consumes health snapshots, failure classifications and
// probe outcomes that were scored elsewhere, and it decides a state
// for each Cog. It has no side effects of its own: it evicts nobody
// from the mesh, rewrites no routing table, schedules no probe and
// persists nothing. A caller acts on the state it reports. Removing a
// Cog permanently additionally requires operator authority, carried as
// a permission token.

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/SyntheticProbe.h>
#include <crucible/topology/AsymmetricFailure.h>
#include <crucible/topology/Health.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/Pinned.h>
#include <foundation/diag/Tag.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::warden {

// foundation::reflect::enum_name gives the log spelling of a state and
// of a signal.
enum class QuarantineState : std::uint8_t {
    Healthy = 0,
    Suspect = 1,
    Quarantined = 2,
    Recovered = 3,
    Permanent = 4,
};

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

struct QuarantineTransition : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "QuarantineTransition";
    static constexpr ::foundation::diag::tag_text_literal description =
        "A Cog changed quarantine/routing-admission state.";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Preserve the transition event, stop routing new work to Quarantined "
        "or Permanent Cogs, and require operator authority for Permanent.";
};

namespace quarantine_tag {

// The authority of an operator to force a state.  A root of this tag is
// that authority, so the row names Init.  Only a startup context mints
// the root, and each split, share and handoff of the token needs a
// context that owns Init.  The hot foreground, a background drain and a
// test runner cannot mint it.
struct OperatorOverride {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::Init>;
};

}  // namespace quarantine_tag

using PositiveRecoveryProbeCount = ::fixy::Positive<std::uint16_t>;
using PositiveNanoseconds = ::fixy::Positive<std::uint64_t>;

struct QuarantineConfig {
    topology::HealthScore suspect_at_or_below{900};
    topology::HealthScore quarantine_at_or_below{500};
    PositiveRecoveryProbeCount recovery_probe_count = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{100});
    PositiveNanoseconds permanent_after_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{3600000000000ull});
    std::uint32_t canary_load_ppm = 10000;
};

struct QuarantineSnapshot {
    cog::Uuid cog_uuid{};
    QuarantineState state = QuarantineState::Healthy;
    topology::HealthScore health_score{};
    ::fixy::Bits<QuarantineSignal> signals{};
    std::uint16_t consecutive_recovery_probes = 0;
    std::uint32_t admitted_load_ppm = 1000000;
    std::uint64_t quarantine_since_ns = 0;
    std::uint64_t sequence = 0;
};

struct QuarantineEvent {
    cog::Uuid cog_uuid{};
    QuarantineState from = QuarantineState::Healthy;
    QuarantineState to = QuarantineState::Healthy;
    topology::HealthScore health_score{};
    ::fixy::Bits<QuarantineSignal> signals{};
    std::uint16_t consecutive_recovery_probes = 0;
    std::uint32_t admitted_load_ppm = 1000000;
    std::uint64_t sequence = 0;
};

// Building a policy belongs to process startup.
template <class Ctx>
concept CtxFitsQuarantineMint = ::foundation::effects::IsExecCtx<Ctx>
                             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// A health, failure or probe fact changes the state from a background
// thread.  The hot foreground records nothing.
template <class Ctx>
concept CtxFitsQuarantineRecord = ::foundation::effects::IsExecCtx<Ctx>
                               && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

// An operator forces a state from startup or from a test.
template <class Ctx>
concept CtxFitsQuarantineOverride = ::foundation::effects::IsExecCtx<Ctx>
                                 && ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Init,
                                                                        ::foundation::effects::Effect::Test>;

namespace detail {

[[nodiscard]] constexpr bool same_uuid(cog::Uuid lhs, cog::Uuid rhs) noexcept {
    return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
}

[[nodiscard]] constexpr std::uint32_t load_for(QuarantineState state, QuarantineConfig const& config) noexcept {
    switch (state) {
        case QuarantineState::Healthy:
            return 1000000;
        case QuarantineState::Suspect:
            return 300000;
        case QuarantineState::Quarantined:
            return 0;
        case QuarantineState::Recovered:
            return config.canary_load_ppm;
        case QuarantineState::Permanent:
            return 0;
        default:
            return 0;
    }
}

[[nodiscard]] constexpr bool critical_health(topology::HealthSnapshot const& snapshot) noexcept {
    return snapshot.issues.test(topology::HealthIssue::UncorrectedEcc)
        || snapshot.issues.test(topology::HealthIssue::ThermalCritical)
        || snapshot.issues.test(topology::HealthIssue::WearCritical);
}

}  // namespace detail

// A policy holds at least one Cog slot, and at least one audit-event slot.
// A policy with no Cog slot holds no state, and a policy with no event slot
// loses each transition that it makes.
template <std::size_t MaxCogs, std::size_t MaxEvents>
concept QuarantineCapacity = (MaxCogs > 0) && (MaxEvents > 0);

template <std::size_t MaxCogs, std::size_t MaxEvents = MaxCogs * 4>
    requires QuarantineCapacity<MaxCogs, MaxEvents>
class QuarantinePolicy;

// The one door: the constructor of the policy is private, and this mint
// is its one friend.
template <std::size_t MaxCogs, std::size_t MaxEvents = MaxCogs * 4, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsQuarantineMint<Ctx>
[[nodiscard]] constexpr QuarantinePolicy<MaxCogs, MaxEvents>
mint_quarantine_policy(Ctx const&, QuarantineConfig config = {}) noexcept;

template <std::size_t MaxCogs, std::size_t MaxEvents>
    requires QuarantineCapacity<MaxCogs, MaxEvents>
class QuarantinePolicy : public ::foundation::Pinned<QuarantinePolicy<MaxCogs, MaxEvents>> {
    struct Slot {
        bool occupied = false;
        cog::Uuid cog_uuid{};
        QuarantineState state = QuarantineState::Healthy;
        topology::HealthScore health_score{};
        ::fixy::Bits<QuarantineSignal> signals{};
        std::uint16_t consecutive_recovery_probes = 0;
        std::uint64_t quarantine_since_ns = 0;
        ::fixy::Monotonic<std::uint64_t> sequence = ::fixy::mint_monotonic<std::uint64_t>(0);
    };

    using SlotIndex = ::fixy::Index<MaxCogs>;
    using EventIndex = ::fixy::Index<MaxEvents>;

    ::foundation::core::FixedArray<Slot, MaxCogs> slots_{};
    ::foundation::core::FixedArray<QuarantineEvent, MaxEvents> events_{};
    // The slot that the next event overwrites.
    EventIndex next_event_ = ::fixy::index<0>();
    std::size_t event_count_ = 0;
    QuarantineConfig config_{};

    explicit constexpr QuarantinePolicy(QuarantineConfig config) noexcept : config_{config} {}

    template <std::size_t Cogs, std::size_t Events, ::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineMint<Ctx>
    friend constexpr QuarantinePolicy<Cogs, Events> mint_quarantine_policy(Ctx const&, QuarantineConfig) noexcept;

    // The slot of the Cog, or a free slot that this call gives to it.  A Cog
    // with no identifier, and a Cog that finds a full table, get no slot.
    // Complexity: O(MaxCogs).
    [[nodiscard]] constexpr ::fixy::Option<SlotIndex> find_or_insert(cog::CogIdentity const& id) noexcept {
        if (id.uuid.is_zero()) {
            return ::fixy::none;
        }
        for (SlotIndex const index : slots_.indices()) {
            Slot const& slot = slots_.at(index);
            if (slot.occupied && detail::same_uuid(slot.cog_uuid, id.uuid)) {
                return ::fixy::some(index);
            }
        }
        for (SlotIndex const index : slots_.indices()) {
            Slot& slot = slots_.at(index);
            if (!slot.occupied) {
                slot.occupied = true;
                slot.cog_uuid = id.uuid;
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // Complexity: O(MaxCogs).
    [[nodiscard]] constexpr ::fixy::Option<SlotIndex> find(cog::CogIdentity const& id) const noexcept {
        for (SlotIndex const index : slots_.indices()) {
            Slot const& slot = slots_.at(index);
            if (slot.occupied && detail::same_uuid(slot.cog_uuid, id.uuid)) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // The slot of the Cog, when the fact carries a sequence after the last
    // sequence of the slot.  The slot then keeps the new sequence.  A fact
    // with an older sequence gets no slot.
    [[nodiscard]] constexpr ::fixy::Option<SlotIndex> admit_fact(cog::CogIdentity const& id,
                                                                 std::uint64_t sequence) noexcept {
        return find_or_insert(id).match(
            [this, sequence](SlotIndex index) noexcept -> ::fixy::Option<SlotIndex> {
                if (!slots_.at(index).sequence.try_advance(sequence)) {
                    return ::fixy::none;
                }
                return ::fixy::some(index);
            },
            []() noexcept -> ::fixy::Option<SlotIndex> { return ::fixy::none; });
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

    constexpr void append_event(Slot const& slot, QuarantineState from, QuarantineState to) noexcept {
        events_.at(next_event_) = QuarantineEvent{
            .cog_uuid = slot.cog_uuid,
            .from = from,
            .to = to,
            .health_score = slot.health_score,
            .signals = slot.signals,
            .consecutive_recovery_probes = slot.consecutive_recovery_probes,
            .admitted_load_ppm = detail::load_for(to, config_),
            .sequence = slot.sequence.get(),
        };
        next_event_ = EventIndex::check((next_event_.raw() + 1u) % MaxEvents)
                          .expect("a remainder of MaxEvents is a slot of the ring");
        if (event_count_ < MaxEvents) {
            ++event_count_;
        }
    }

    constexpr void transition_to(Slot& slot, QuarantineState next, std::uint64_t now_ns) noexcept {
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

    [[nodiscard]] constexpr QuarantineState state_from_health(Slot const& slot, topology::HealthSnapshot const& health,
                                                              ::fixy::Bits<QuarantineSignal>& signals) const noexcept {
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
        if (health.state == topology::HealthState::Suspect || health.score.raw() <= config_.suspect_at_or_below.raw()) {
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
    [[nodiscard]] constexpr QuarantineConfig config() const noexcept { return config_; }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool on_health_event(Ctx const&, cog::CogIdentity const& cog,
                                                 topology::HealthSnapshot health, std::uint64_t now_ns) noexcept {
        return admit_fact(cog, health.sequence)
            .match(
                [this, &health, now_ns](SlotIndex index) noexcept {
                    Slot& slot = slots_.at(index);
                    slot.health_score = health.score;
                    slot.signals = {};
                    QuarantineState const next = state_from_health(slot, health, slot.signals);
                    transition_to(slot, next, now_ns);
                    return true;
                },
                []() noexcept { return false; });
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool on_asymmetric_failure(Ctx const&, cog::CogIdentity const& cog,
                                                       topology::FailureClass cls, std::uint64_t now_ns,
                                                       std::uint64_t sequence) noexcept {
        return admit_fact(cog, sequence)
            .match(
                [this, cls, now_ns](SlotIndex index) noexcept {
                    Slot& slot = slots_.at(index);
                    slot.signals = {};
                    QuarantineState next = slot.state;
                    if (cls == topology::FailureClass::BidiFailed) {
                        slot.signals.set(QuarantineSignal::AsymmetricDead);
                        next = QuarantineState::Quarantined;
                    } else if (cls == topology::FailureClass::TxBroken || cls == topology::FailureClass::RxBroken) {
                        slot.signals.set(QuarantineSignal::AsymmetricSuspect);
                        if (slot.state == QuarantineState::Healthy || slot.state == QuarantineState::Recovered) {
                            next = QuarantineState::Suspect;
                        }
                    }
                    transition_to(slot, next, now_ns);
                    return true;
                },
                []() noexcept { return false; });
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool record_recovery_probe(Ctx const&, cog::CogIdentity const& cog,
                                                       observe::ProbeOutcome outcome, std::uint64_t now_ns) noexcept {
        return admit_fact(cog, outcome.sequence)
            .match(
                [this, &outcome, now_ns](SlotIndex index) noexcept {
                    Slot& slot = slots_.at(index);
                    slot.signals = {};
                    if (slot.state != QuarantineState::Quarantined) {
                        return true;
                    }
                    if (outcome.ok()) {
                        slot.signals.set(QuarantineSignal::RecoveryProbePassed);
                        if (slot.consecutive_recovery_probes < config_.recovery_probe_count.value()) {
                            ++slot.consecutive_recovery_probes;
                        }
                        if (slot.consecutive_recovery_probes >= config_.recovery_probe_count.value()) {
                            slot.signals.set(QuarantineSignal::RecoveryThresholdMet);
                            transition_to(slot, QuarantineState::Recovered, now_ns);
                        }
                    } else {
                        slot.signals.set(QuarantineSignal::RecoveryProbeFailed);
                        slot.consecutive_recovery_probes = 0;
                    }
                    return true;
                },
                []() noexcept { return false; });
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsQuarantineRecord<Ctx>
    [[nodiscard]] constexpr bool check_permanent_deadline(Ctx const&, cog::CogIdentity const& cog, std::uint64_t now_ns,
                                                          std::uint64_t sequence) noexcept {
        return admit_fact(cog, sequence)
            .match(
                [this, now_ns](SlotIndex index) noexcept {
                    Slot& slot = slots_.at(index);
                    slot.signals = {};
                    if (slot.state != QuarantineState::Quarantined || slot.quarantine_since_ns == 0
                        || now_ns < slot.quarantine_since_ns
                        || now_ns - slot.quarantine_since_ns < config_.permanent_after_ns.value()) {
                        return true;
                    }
                    slot.signals.set(QuarantineSignal::PermanentTimerElapsed);
                    slot.signals.set(QuarantineSignal::PermanentRequiresOperator);
                    append_event(slot, slot.state, slot.state);
                    return true;
                },
                []() noexcept { return false; });
    }

    // The authority goes back to the caller with its own brand, so a
    // caller holds one token across many overrides.
    template <::foundation::effects::IsExecCtx Ctx, class Brand>
        requires CtxFitsQuarantineOverride<Ctx>
    [[nodiscard]] constexpr ::foundation::permissions::Permission<quarantine_tag::OperatorOverride, Brand>
    operator_override(Ctx const&,
                      ::foundation::permissions::Permission<quarantine_tag::OperatorOverride, Brand>&& authority,
                      cog::CogIdentity const& cog, QuarantineState forced, std::uint64_t now_ns,
                      std::uint64_t sequence) noexcept {
        admit_fact(cog, sequence)
            .match(
                [this, forced, now_ns](SlotIndex index) noexcept {
                    Slot& slot = slots_.at(index);
                    slot.signals = {};
                    slot.signals.set(QuarantineSignal::OperatorOverride);
                    transition_to(slot, forced, now_ns);
                },
                []() noexcept {});
        return std::move(authority);
    }

    [[nodiscard]] constexpr QuarantineState state(cog::CogIdentity const& id) const noexcept {
        return find(id).match([this](SlotIndex index) noexcept { return slots_.at(index).state; },
                              []() noexcept { return QuarantineState::Healthy; });
    }

    [[nodiscard]] constexpr QuarantineSnapshot current(cog::CogIdentity const& id) const noexcept {
        return find(id).match([this](SlotIndex index) noexcept { return snapshot(slots_.at(index)); },
                              [&id]() noexcept {
                                  QuarantineSnapshot missing{};
                                  missing.cog_uuid = id.uuid;
                                  return missing;
                              });
    }

    [[nodiscard]] constexpr std::size_t transition_event_count() const noexcept { return event_count_; }

    // The event at index, counted from the oldest event that the ring
    // keeps, or no value at or past the count.
    [[nodiscard]] constexpr ::fixy::Option<QuarantineEvent> transition_event_at(std::size_t index) const noexcept {
        if (index >= event_count_) {
            return ::fixy::none;
        }
        // index counts from the oldest surviving event, which is not
        // slot zero once the ring has wrapped. Before the wrap the
        // write cursor equals the count, so the subtraction is zero
        // modulo the size and the base is slot zero. After the wrap
        // the write cursor is itself the oldest surviving slot.
        const std::size_t base = (next_event_.raw() + MaxEvents - event_count_) % MaxEvents;
        return EventIndex::check((base + index) % MaxEvents)
            .match([this](EventIndex slot) noexcept { return ::fixy::some(events_.at(slot)); },
                   []() noexcept -> ::fixy::Option<QuarantineEvent> { return ::fixy::none; });
    }
};

template <std::size_t MaxCogs, std::size_t MaxEvents, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsQuarantineMint<Ctx>
[[nodiscard]] constexpr QuarantinePolicy<MaxCogs, MaxEvents> mint_quarantine_policy(Ctx const&,
                                                                                    QuarantineConfig config) noexcept {
    return QuarantinePolicy<MaxCogs, MaxEvents>{config};
}

}  // namespace crucible::warden
