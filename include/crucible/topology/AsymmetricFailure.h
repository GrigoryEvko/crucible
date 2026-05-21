#pragma once

// GAPS-127. Bounded asymmetric/gray failure classification.
//
// This header consumes already-measured probe outcomes. It does not send
// packets, poll witnesses over the network, or mutate membership state. Those
// side effects belong to CNT/PERF transport tasks and Canopy membership tasks.
// The invariant here is one typed, bounded classification substrate over
// local bidirectional probe legs plus multi-vantage witness reports.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/observe/SyntheticProbe.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/topology/Health.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

enum class FailureClass : std::uint8_t {
    BidiOk = 0,
    TxBroken = 1,
    RxBroken = 2,
    BidiFailed = 3,
    Inconclusive = 4,
};

[[nodiscard]] std::string_view failure_class_name(FailureClass cls) noexcept;

enum class FailureSignal : std::uint32_t {
    LocalOutboundFailed = 1u << 0,
    LocalInboundFailed = 1u << 1,
    WitnessReachable = 1u << 2,
    WitnessUnreachable = 1u << 3,
    WitnessMajorityReachable = 1u << 4,
    WitnessMajorityUnreachable = 1u << 5,
    InsufficientLocalSamples = 1u << 6,
    InsufficientWitnesses = 1u << 7,
};

[[nodiscard]] std::string_view failure_signal_name(FailureSignal signal) noexcept;

struct AsymmetricFailureDetected : safety::diag::tag_base {
    static constexpr std::string_view name = "AsymmetricFailureDetected";
    static constexpr std::string_view description =
        "Bidirectional probes or witness votes detected a one-way or gray path failure.";
    static constexpr std::string_view remediation =
        "Prefer path-swap or route-around policy before quarantining the peer; "
        "only quarantine when multi-vantage votes agree that the peer is dead.";
};

using PositiveProbeSamples = safety::Positive<std::uint16_t>;

struct AsymmetricFailurePolicy {
    PositiveProbeSamples min_local_samples{std::uint16_t{2}};
    PositiveProbeSamples min_witnesses{std::uint16_t{1}};
};

struct DirectionWindow {
    std::uint16_t count = 0;
    std::uint16_t next = 0;
    std::uint16_t successes = 0;
};

struct FailureSummary {
    cog::Uuid peer_uuid{};
    FailureClass local = FailureClass::Inconclusive;
    FailureClass with_witnesses = FailureClass::Inconclusive;
    safety::Bits<FailureSignal> signals{};
    std::uint16_t outbound_samples = 0;
    std::uint16_t inbound_samples = 0;
    std::uint16_t outbound_successes = 0;
    std::uint16_t inbound_successes = 0;
    std::uint16_t witnesses = 0;
    std::uint16_t witness_reachable = 0;
    std::uint64_t sequence = 0;
};

struct AsymmetricFailureEvent {
    cog::Uuid peer_uuid{};
    FailureClass from = FailureClass::Inconclusive;
    FailureClass to = FailureClass::Inconclusive;
    safety::Bits<FailureSignal> signals{};
    std::uint16_t witnesses = 0;
    std::uint16_t witness_reachable = 0;
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsAsymmetricFailureMint =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

template <class Ctx>
concept CtxFitsAsymmetricFailureRecord =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Bg>;

namespace detail {

enum class LegState : std::uint8_t {
    Unknown = 0,
    Passing = 1,
    Failing = 2,
};

[[nodiscard]] constexpr bool same_cog_uuid(cog::Uuid lhs, cog::Uuid rhs) noexcept {
    return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
}

[[nodiscard]] constexpr LegState majority_state(DirectionWindow window,
                                                std::uint16_t min_samples) noexcept {
    if (window.count < min_samples) {
        return LegState::Unknown;
    }
    std::uint16_t const failures =
        static_cast<std::uint16_t>(window.count - window.successes);
    if (window.successes > failures) {
        return LegState::Passing;
    }
    if (failures > window.successes) {
        return LegState::Failing;
    }
    return LegState::Unknown;
}

}  // namespace detail

template <std::size_t MaxPeers, std::size_t Window = 8, std::size_t MaxWitnesses = 8>
class AsymmetricFailureDetector
    : public safety::Pinned<AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>> {
    static_assert(MaxPeers > 0, "AsymmetricFailureDetector requires peer slots");
    static_assert(Window > 0, "AsymmetricFailureDetector requires a non-empty probe window");
    static_assert(Window <= 8, "AsymmetricFailureDetector local state is cache-line sized for Window <= 8");
    static_assert(MaxWitnesses > 0, "AsymmetricFailureDetector requires witness slots");

    struct WitnessSlot {
        bool occupied = false;
        cog::Uuid witness_uuid{};
        bool reachable = false;
        safety::Monotonic<std::uint64_t> sequence{0};
    };

    struct alignas(64) PeerSlot {
        bool occupied = false;
        cog::Uuid peer_uuid{};
        std::array<bool, Window> outbound{};
        std::array<bool, Window> inbound{};
        DirectionWindow outbound_window{};
        DirectionWindow inbound_window{};
        FailureClass last_class = FailureClass::Inconclusive;
        safety::Monotonic<std::uint64_t> sequence{0};
    };
    static_assert(sizeof(PeerSlot) <= 64,
        "AsymmetricFailureDetector keeps local peer state within one cache line");

    std::array<PeerSlot, MaxPeers> peers_{};
    std::array<std::array<WitnessSlot, MaxWitnesses>, MaxPeers> witnesses_{};
    std::array<AsymmetricFailureEvent, MaxPeers * 4> events_{};
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    AsymmetricFailurePolicy policy_{};

    [[nodiscard]] constexpr PeerSlot* find_or_insert(cog::CogIdentity const& peer) noexcept {
        if (peer.uuid.is_zero()) {
            return nullptr;
        }
        for (auto& slot : peers_) {
            if (slot.occupied && detail::same_cog_uuid(slot.peer_uuid, peer.uuid)) {
                return &slot;
            }
        }
        for (auto& slot : peers_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.peer_uuid = peer.uuid;
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr PeerSlot const* find(cog::CogIdentity const& peer) const noexcept {
        for (auto const& slot : peers_) {
            if (slot.occupied && detail::same_cog_uuid(slot.peer_uuid, peer.uuid)) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr std::size_t slot_index(PeerSlot const& slot) const noexcept {
        return static_cast<std::size_t>(&slot - peers_.data());
    }

    static constexpr void push_sample(std::array<bool, Window>& ring,
                                      DirectionWindow& window,
                                      bool success) noexcept {
        if (window.count == Window) {
            if (ring[window.next]) {
                --window.successes;
            }
        } else {
            ++window.count;
        }
        ring[window.next] = success;
        if (success) {
            ++window.successes;
        }
        window.next = static_cast<std::uint16_t>((window.next + 1u) % Window);
    }

    [[nodiscard]] constexpr FailureSummary summarize(PeerSlot const& slot) const noexcept {
        FailureSummary out{};
        out.peer_uuid = slot.peer_uuid;
        out.outbound_samples = slot.outbound_window.count;
        out.inbound_samples = slot.inbound_window.count;
        out.outbound_successes = slot.outbound_window.successes;
        out.inbound_successes = slot.inbound_window.successes;
        out.sequence = slot.sequence.get();

        std::uint16_t const min_samples = policy_.min_local_samples.value();
        detail::LegState const outbound =
            detail::majority_state(slot.outbound_window, min_samples);
        detail::LegState const inbound =
            detail::majority_state(slot.inbound_window, min_samples);

        if (outbound == detail::LegState::Unknown
            || inbound == detail::LegState::Unknown) {
            out.local = FailureClass::Inconclusive;
            out.signals.set(FailureSignal::InsufficientLocalSamples);
        } else if (outbound == detail::LegState::Passing
                   && inbound == detail::LegState::Passing) {
            out.local = FailureClass::BidiOk;
        } else if (outbound == detail::LegState::Failing
                   && inbound == detail::LegState::Passing) {
            out.local = FailureClass::TxBroken;
            out.signals.set(FailureSignal::LocalOutboundFailed);
        } else if (outbound == detail::LegState::Passing
                   && inbound == detail::LegState::Failing) {
            out.local = FailureClass::RxBroken;
            out.signals.set(FailureSignal::LocalInboundFailed);
        } else {
            out.local = FailureClass::BidiFailed;
            out.signals.set(FailureSignal::LocalOutboundFailed);
            out.signals.set(FailureSignal::LocalInboundFailed);
        }

        auto const& witnesses = witnesses_[slot_index(slot)];
        for (auto const& witness : witnesses) {
            if (!witness.occupied) {
                continue;
            }
            ++out.witnesses;
            if (witness.reachable) {
                ++out.witness_reachable;
                out.signals.set(FailureSignal::WitnessReachable);
            } else {
                out.signals.set(FailureSignal::WitnessUnreachable);
            }
        }

        out.with_witnesses = out.local;
        std::uint16_t const min_witnesses = policy_.min_witnesses.value();
        if (out.witnesses < min_witnesses) {
            out.signals.set(FailureSignal::InsufficientWitnesses);
            return out;
        }

        std::uint16_t const quorum =
            static_cast<std::uint16_t>((out.witnesses + 1u) / 2u);
        if (out.witness_reachable >= quorum) {
            out.signals.set(FailureSignal::WitnessMajorityReachable);
            if (out.local == FailureClass::BidiFailed
                || out.local == FailureClass::Inconclusive) {
                out.with_witnesses = FailureClass::TxBroken;
            }
        } else {
            out.signals.set(FailureSignal::WitnessMajorityUnreachable);
            if (out.local == FailureClass::BidiOk) {
                out.with_witnesses = FailureClass::Inconclusive;
            }
        }
        return out;
    }

    constexpr void append_event(PeerSlot& slot,
                                FailureClass from,
                                FailureSummary const& summary) noexcept {
        events_[next_event_] = AsymmetricFailureEvent{
            .peer_uuid = slot.peer_uuid,
            .from = from,
            .to = summary.with_witnesses,
            .signals = summary.signals,
            .witnesses = summary.witnesses,
            .witness_reachable = summary.witness_reachable,
            .sequence = summary.sequence,
        };
        next_event_ = (next_event_ + 1u) % events_.size();
        if (event_count_ < events_.size()) {
            ++event_count_;
        }
        slot.last_class = summary.with_witnesses;
    }

    constexpr void record_if_transition(PeerSlot& slot) noexcept {
        FailureSummary const summary = summarize(slot);
        if (summary.with_witnesses != slot.last_class) {
            append_event(slot, slot.last_class, summary);
        }
    }

public:
    explicit constexpr AsymmetricFailureDetector(
        AsymmetricFailurePolicy policy = {}) noexcept
        : policy_{policy} {}

    [[nodiscard]] constexpr AsymmetricFailurePolicy policy() const noexcept {
        return policy_;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_outbound(Ctx const&,
                                                 cog::CogIdentity const& peer,
                                                 bool success,
                                                 std::uint64_t sequence) noexcept {
        PeerSlot* slot = find_or_insert(peer);
        if (slot == nullptr || !slot->sequence.try_advance(sequence)) {
            return false;
        }
        push_sample(slot->outbound, slot->outbound_window, success);
        record_if_transition(*slot);
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_inbound(Ctx const&,
                                                cog::CogIdentity const& peer,
                                                bool success,
                                                std::uint64_t sequence) noexcept {
        PeerSlot* slot = find_or_insert(peer);
        if (slot == nullptr || !slot->sequence.try_advance(sequence)) {
            return false;
        }
        push_sample(slot->inbound, slot->inbound_window, success);
        record_if_transition(*slot);
        return true;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_synthetic_round(
        Ctx const& ctx,
        cog::CogIdentity const& peer,
        observe::ProbeOutcome outbound,
        observe::ProbeOutcome inbound) noexcept {
        return record_outbound(ctx, peer, outbound.ok(), outbound.sequence)
            && record_inbound(ctx, peer, inbound.ok(),
                std::max(outbound.sequence, inbound.sequence));
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_witness(Ctx const&,
                                                cog::CogIdentity const& target,
                                                cog::CogIdentity const& witness,
                                                bool witness_reaches_target,
                                                std::uint64_t sequence) noexcept {
        if (witness.uuid.is_zero()) {
            return false;
        }
        PeerSlot* slot = find_or_insert(target);
        if (slot == nullptr || sequence < slot->sequence.get()) {
            return false;
        }
        auto& witnesses = witnesses_[slot_index(*slot)];
        for (auto& existing : witnesses) {
            if (existing.occupied
                && detail::same_cog_uuid(existing.witness_uuid, witness.uuid)) {
                if (sequence < existing.sequence.get()) {
                    return false;
                }
                (void)slot->sequence.try_advance(sequence);
                (void)existing.sequence.try_advance(sequence);
                existing.reachable = witness_reaches_target;
                record_if_transition(*slot);
                return true;
            }
        }
        for (auto& existing : witnesses) {
            if (!existing.occupied) {
                existing.occupied = true;
                existing.witness_uuid = witness.uuid;
                existing.reachable = witness_reaches_target;
                (void)slot->sequence.try_advance(sequence);
                (void)existing.sequence.try_advance(sequence);
                record_if_transition(*slot);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr FailureClass
    classify(cog::CogIdentity const& peer) const noexcept {
        PeerSlot const* slot = find(peer);
        if (slot == nullptr) {
            return FailureClass::Inconclusive;
        }
        return summarize(*slot).local;
    }

    [[nodiscard]] constexpr FailureClass
    classify_with_witnesses(cog::CogIdentity const& peer) const noexcept {
        PeerSlot const* slot = find(peer);
        if (slot == nullptr) {
            return FailureClass::Inconclusive;
        }
        return summarize(*slot).with_witnesses;
    }

    [[nodiscard]] constexpr FailureSummary
    summary(cog::CogIdentity const& peer) const noexcept {
        PeerSlot const* slot = find(peer);
        if (slot == nullptr) {
            FailureSummary missing{};
            missing.peer_uuid = peer.uuid;
            missing.signals.set(FailureSignal::InsufficientLocalSamples);
            missing.signals.set(FailureSignal::InsufficientWitnesses);
            return missing;
        }
        return summarize(*slot);
    }

    [[nodiscard]] constexpr std::span<const AsymmetricFailureEvent>
    events() const noexcept {
        return std::span<const AsymmetricFailureEvent>{events_.data(), event_count_};
    }

    [[nodiscard]] constexpr std::size_t event_count() const noexcept {
        return event_count_;
    }
};

[[nodiscard]] constexpr HealthState
health_state_for_failure(FailureClass cls, HealthState fallback) noexcept {
    switch (cls) {
        case FailureClass::BidiOk:       return fallback;
        case FailureClass::TxBroken:     return HealthState::Suspect;
        case FailureClass::RxBroken:     return HealthState::Suspect;
        case FailureClass::BidiFailed:   return HealthState::Quarantined;
        case FailureClass::Inconclusive: return fallback;
        default:                         return fallback;
    }
}

template <effects::IsExecCtx Ctx,
          std::size_t MaxPeers,
          std::size_t Window = 8,
          std::size_t MaxWitnesses = 8>
    requires CtxFitsAsymmetricFailureMint<Ctx>
[[nodiscard]] constexpr AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>
mint_asymmetric_failure_detector(Ctx const&,
                                 AsymmetricFailurePolicy policy = {}) noexcept {
    return AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>{policy};
}

static_assert(safety::diag::is_diagnostic_class_v<AsymmetricFailureDetected>);
static_assert(sizeof(DirectionWindow) == 6);
static_assert(std::is_trivially_copyable_v<FailureSummary>);
static_assert(std::is_trivially_copyable_v<AsymmetricFailureEvent>);
static_assert(sizeof(AsymmetricFailureEvent) <= 64);
static_assert(CtxFitsAsymmetricFailureMint<effects::ColdInitCtx>);
static_assert(!CtxFitsAsymmetricFailureMint<effects::BgDrainCtx>);
static_assert(CtxFitsAsymmetricFailureRecord<effects::BgDrainCtx>);
static_assert(!CtxFitsAsymmetricFailureRecord<effects::HotFgCtx>);

}  // namespace crucible::topology
