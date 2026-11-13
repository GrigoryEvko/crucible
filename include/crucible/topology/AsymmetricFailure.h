#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/SyntheticProbe.h>
#include <crucible/topology/Health.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/Pinned.h>
#include <foundation/core/Array.h>
#include <foundation/diag/Tag.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::topology {

// foundation::reflect::enum_name gives the log spelling of both enums.
enum class FailureClass : std::uint8_t {
    BidiOk = 0,
    TxBroken = 1,
    RxBroken = 2,
    BidiFailed = 3,
    Inconclusive = 4,
};

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

struct AsymmetricFailureDetected : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "AsymmetricFailureDetected";
    static constexpr ::foundation::diag::tag_text_literal description =
        "Bidirectional probes or witness votes detected a one-way or gray path failure.";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Prefer path-swap or route-around policy before quarantining the peer; "
        "only quarantine when multi-vantage votes agree that the peer is dead.";
};

using PositiveProbeSamples = ::fixy::Positive<std::uint16_t>;

struct AsymmetricFailurePolicy {
    PositiveProbeSamples min_local_samples = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{2});
    PositiveProbeSamples min_witnesses = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
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
    ::fixy::Bits<FailureSignal> signals{};
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
    ::fixy::Bits<FailureSignal> signals{};
    std::uint16_t witnesses = 0;
    std::uint16_t witness_reachable = 0;
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsAsymmetricFailureMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsAsymmetricFailureRecord =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

namespace detail {

enum class LegState : std::uint8_t {
    Unknown = 0,
    Passing = 1,
    Failing = 2,
};

[[nodiscard]] constexpr LegState majority_state(DirectionWindow window, std::uint16_t min_samples) noexcept {
    if (window.count < min_samples) {
        return LegState::Unknown;
    }
    std::uint16_t const failures = static_cast<std::uint16_t>(window.count - window.successes);
    if (window.successes > failures) {
        return LegState::Passing;
    }
    if (failures > window.successes) {
        return LegState::Failing;
    }
    return LegState::Unknown;
}

}  // namespace detail

template <std::size_t MaxPeers, std::size_t Window, std::size_t MaxWitnesses>
class AsymmetricFailureDetector;

// The only door into a detector.  An Init-row context mints it; a
// background worker then records probes and witness votes on it.
template <class Ctx, std::size_t MaxPeers, std::size_t Window = 8, std::size_t MaxWitnesses = 8>
    requires CtxFitsAsymmetricFailureMint<Ctx>
[[nodiscard]] constexpr AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>
mint_asymmetric_failure_detector(Ctx const&, AsymmetricFailurePolicy policy = {}) noexcept;

template <std::size_t MaxPeers, std::size_t Window, std::size_t MaxWitnesses>
class AsymmetricFailureDetector
    : public ::foundation::Pinned<AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>> {
    static_assert(MaxPeers > 0, "AsymmetricFailureDetector requires peer slots");
    static_assert(Window > 0, "AsymmetricFailureDetector requires a non-empty probe window");
    static_assert(Window <= 8, "AsymmetricFailureDetector local state is cache-line sized for Window <= 8");
    static_assert(MaxWitnesses > 0, "AsymmetricFailureDetector requires witness slots");

    // The slots of a probe ring: Window, and one slot for a zero Window, so
    // a zero Window fails only at its static_assert above.
    static constexpr std::size_t kRingSlots = Window == 0 ? 1 : Window;

    struct WitnessSlot {
        bool occupied = false;
        cog::Uuid witness_uuid{};
        bool reachable = false;
        MonotoneCount sequence = ::fixy::mint_monotonic<std::uint64_t>(0);
    };

    struct alignas(64) PeerSlot {
        bool occupied = false;
        cog::Uuid peer_uuid{};
        ::foundation::core::FixedArray<bool, kRingSlots> outbound{};
        ::foundation::core::FixedArray<bool, kRingSlots> inbound{};
        DirectionWindow outbound_window{};
        DirectionWindow inbound_window{};
        FailureClass last_class = FailureClass::Inconclusive;
        MonotoneCount sequence = ::fixy::mint_monotonic<std::uint64_t>(0);
    };
    static_assert(sizeof(PeerSlot) <= 64, "AsymmetricFailureDetector keeps local peer state within one cache line");

    static constexpr std::size_t kMaxEvents = MaxPeers * 4;

    ::foundation::core::FixedArray<PeerSlot, MaxPeers> peers_{};
    ::foundation::core::FixedArray<::foundation::core::FixedArray<WitnessSlot, MaxWitnesses>, MaxPeers> witnesses_{};
    ::foundation::core::FixedArray<AsymmetricFailureEvent, kMaxEvents> events_{};
    ::fixy::Index<kMaxEvents> next_event_ = ::fixy::index<0>();
    std::size_t event_count_ = 0;
    AsymmetricFailurePolicy policy_{};

    template <class Ctx, std::size_t Peers, std::size_t W, std::size_t Witnesses>
        requires CtxFitsAsymmetricFailureMint<Ctx>
    friend constexpr AsymmetricFailureDetector<Peers, W, Witnesses>
    mint_asymmetric_failure_detector(Ctx const&, AsymmetricFailurePolicy policy) noexcept;

    explicit constexpr AsymmetricFailureDetector(AsymmetricFailurePolicy policy) noexcept : policy_{policy} {}

    // The index of the occupied slot of this peer, or no value.
    // Complexity: linear in the slots.
    [[nodiscard]] constexpr ::fixy::Option<::fixy::Index<MaxPeers>> find(cog::Uuid const& uuid) const noexcept {
        for (::fixy::Index<MaxPeers> const index : peers_.indices()) {
            PeerSlot const& slot = peers_.at(index);
            if (slot.occupied && slot.peer_uuid == uuid) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // The index of the slot of this peer, claimed from a free slot on first
    // sight.  A zero identity is never a peer.
    [[nodiscard]] constexpr ::fixy::Option<::fixy::Index<MaxPeers>>
    find_or_insert(cog::CogIdentity const& peer) noexcept {
        if (peer.uuid.is_zero()) {
            return ::fixy::none;
        }
        ::fixy::Option<::fixy::Index<MaxPeers>> found = find(peer.uuid);
        if (found.is_some()) {
            return found;
        }
        for (::fixy::Index<MaxPeers> const index : peers_.indices()) {
            PeerSlot& slot = peers_.at(index);
            if (!slot.occupied) {
                slot.occupied = true;
                slot.peer_uuid = peer.uuid;
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // The cursor of a window is below Window in each state, so its check
    // cannot fail.
    static constexpr void push_sample(::foundation::core::FixedArray<bool, kRingSlots>& ring, DirectionWindow& window,
                                      bool success) noexcept {
        ::fixy::Index<kRingSlots> const next =
            ::fixy::Index<kRingSlots>::check(window.next).expect("the cursor of a probe window is below its size");
        if (window.count == Window) {
            if (ring.at(next)) {
                --window.successes;
            }
        } else {
            ++window.count;
        }
        ring.at(next) = success;
        if (success) {
            ++window.successes;
        }
        window.next = static_cast<std::uint16_t>(detail::next_ring_index(next).raw());
    }

    [[nodiscard]] constexpr FailureSummary summarize(::fixy::Index<MaxPeers> index) const noexcept {
        PeerSlot const& slot = peers_.at(index);
        FailureSummary out{};
        out.peer_uuid = slot.peer_uuid;
        out.outbound_samples = slot.outbound_window.count;
        out.inbound_samples = slot.inbound_window.count;
        out.outbound_successes = slot.outbound_window.successes;
        out.inbound_successes = slot.inbound_window.successes;
        out.sequence = slot.sequence.get();

        std::uint16_t const min_samples = policy_.min_local_samples.value();
        detail::LegState const outbound = detail::majority_state(slot.outbound_window, min_samples);
        detail::LegState const inbound = detail::majority_state(slot.inbound_window, min_samples);

        if (outbound == detail::LegState::Unknown || inbound == detail::LegState::Unknown) {
            out.local = FailureClass::Inconclusive;
            out.signals.set(FailureSignal::InsufficientLocalSamples);
        } else if (outbound == detail::LegState::Passing && inbound == detail::LegState::Passing) {
            out.local = FailureClass::BidiOk;
        } else if (outbound == detail::LegState::Failing && inbound == detail::LegState::Passing) {
            out.local = FailureClass::TxBroken;
            out.signals.set(FailureSignal::LocalOutboundFailed);
        } else if (outbound == detail::LegState::Passing && inbound == detail::LegState::Failing) {
            out.local = FailureClass::RxBroken;
            out.signals.set(FailureSignal::LocalInboundFailed);
        } else {
            out.local = FailureClass::BidiFailed;
            out.signals.set(FailureSignal::LocalOutboundFailed);
            out.signals.set(FailureSignal::LocalInboundFailed);
        }

        for (WitnessSlot const& witness : witnesses_.at(index)) {
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

        std::uint16_t const quorum = static_cast<std::uint16_t>((out.witnesses + 1u) / 2u);
        if (out.witness_reachable >= quorum) {
            out.signals.set(FailureSignal::WitnessMajorityReachable);
            if (out.local == FailureClass::BidiFailed || out.local == FailureClass::Inconclusive) {
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

    constexpr void append_event(PeerSlot& slot, FailureClass from, FailureSummary const& summary) noexcept {
        events_.at(next_event_) = AsymmetricFailureEvent{
            .peer_uuid = slot.peer_uuid,
            .from = from,
            .to = summary.with_witnesses,
            .signals = summary.signals,
            .witnesses = summary.witnesses,
            .witness_reachable = summary.witness_reachable,
            .sequence = summary.sequence,
        };
        next_event_ = detail::next_ring_index(next_event_);
        if (event_count_ < kMaxEvents) {
            ++event_count_;
        }
        slot.last_class = summary.with_witnesses;
    }

    constexpr void record_if_transition(::fixy::Index<MaxPeers> index) noexcept {
        FailureSummary const summary = summarize(index);
        PeerSlot& slot = peers_.at(index);
        if (summary.with_witnesses != slot.last_class) {
            append_event(slot, slot.last_class, summary);
        }
    }

    // Pushes one probe sample into the outbound or the inbound window of
    // the peer, after the sequence advances.
    [[nodiscard]] constexpr bool record_probe(cog::CogIdentity const& peer, bool is_outbound, bool success,
                                              std::uint64_t sequence) noexcept {
        return find_or_insert(peer).match(
            [&](::fixy::Index<MaxPeers> index) {
                PeerSlot& slot = peers_.at(index);
                if (!slot.sequence.try_advance(sequence)) {
                    return false;
                }
                if (is_outbound) {
                    push_sample(slot.outbound, slot.outbound_window, success);
                } else {
                    push_sample(slot.inbound, slot.inbound_window, success);
                }
                record_if_transition(index);
                return true;
            },
            [] { return false; });
    }

    // Records the vote of witness for the peer of index.  A vote older than
    // the last vote of the witness, or a new witness with no free slot, is
    // refused.
    [[nodiscard]] constexpr bool record_vote(::fixy::Index<MaxPeers> index, cog::CogIdentity const& witness,
                                             bool witness_reaches_target, std::uint64_t sequence) noexcept {
        PeerSlot& slot = peers_.at(index);
        if (sequence < slot.sequence.get()) {
            return false;
        }
        ::foundation::core::FixedArray<WitnessSlot, MaxWitnesses>& witnesses = witnesses_.at(index);
        for (WitnessSlot& existing : witnesses) {
            if (existing.occupied && existing.witness_uuid == witness.uuid) {
                if (sequence < existing.sequence.get()) {
                    return false;
                }
                static_cast<void>(slot.sequence.try_advance(sequence));
                static_cast<void>(existing.sequence.try_advance(sequence));
                existing.reachable = witness_reaches_target;
                record_if_transition(index);
                return true;
            }
        }
        for (WitnessSlot& existing : witnesses) {
            if (!existing.occupied) {
                existing.occupied = true;
                existing.witness_uuid = witness.uuid;
                existing.reachable = witness_reaches_target;
                static_cast<void>(slot.sequence.try_advance(sequence));
                static_cast<void>(existing.sequence.try_advance(sequence));
                record_if_transition(index);
                return true;
            }
        }
        return false;
    }

public:
    [[nodiscard]] constexpr AsymmetricFailurePolicy policy() const noexcept { return policy_; }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_outbound(Ctx const&, cog::CogIdentity const& peer, bool success,
                                                 std::uint64_t sequence) noexcept {
        return record_probe(peer, true, success, sequence);
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_inbound(Ctx const&, cog::CogIdentity const& peer, bool success,
                                                std::uint64_t sequence) noexcept {
        return record_probe(peer, false, success, sequence);
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_synthetic_round(Ctx const& ctx, cog::CogIdentity const& peer,
                                                        observe::ProbeOutcome outbound,
                                                        observe::ProbeOutcome inbound) noexcept {
        return record_outbound(ctx, peer, outbound.ok(), outbound.sequence)
            && record_inbound(ctx, peer, inbound.ok(), ::fixy::max(outbound.sequence, inbound.sequence));
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsAsymmetricFailureRecord<Ctx>
    [[nodiscard]] constexpr bool record_witness(Ctx const&, cog::CogIdentity const& target,
                                                cog::CogIdentity const& witness, bool witness_reaches_target,
                                                std::uint64_t sequence) noexcept {
        if (witness.uuid.is_zero()) {
            return false;
        }
        return find_or_insert(target).match(
            [&](::fixy::Index<MaxPeers> index) {
                return record_vote(index, witness, witness_reaches_target, sequence);
            },
            [] { return false; });
    }

    [[nodiscard]] constexpr FailureClass classify(cog::CogIdentity const& peer) const noexcept {
        return find(peer.uuid).match([&](::fixy::Index<MaxPeers> index) { return summarize(index).local; },
                                     [] { return FailureClass::Inconclusive; });
    }

    [[nodiscard]] constexpr FailureClass classify_with_witnesses(cog::CogIdentity const& peer) const noexcept {
        return find(peer.uuid).match([&](::fixy::Index<MaxPeers> index) { return summarize(index).with_witnesses; },
                                     [] { return FailureClass::Inconclusive; });
    }

    [[nodiscard]] constexpr FailureSummary summary(cog::CogIdentity const& peer) const noexcept {
        return find(peer.uuid).match([&](::fixy::Index<MaxPeers> index) { return summarize(index); },
                                     [&] {
                                         FailureSummary missing{};
                                         missing.peer_uuid = peer.uuid;
                                         missing.signals.set(FailureSignal::InsufficientLocalSamples);
                                         missing.signals.set(FailureSignal::InsufficientWitnesses);
                                         return missing;
                                     });
    }

    // The events in the order of their slots in the ring.
    [[nodiscard]] constexpr ::fixy::View<AsymmetricFailureEvent const> events() const noexcept {
        return events_.view().window(0, event_count_).value_or(::fixy::View<AsymmetricFailureEvent const>{});
    }

    [[nodiscard]] constexpr std::size_t event_count() const noexcept { return event_count_; }
};

[[nodiscard]] constexpr HealthState health_state_for_failure(FailureClass cls, HealthState fallback) noexcept {
    switch (cls) {
        case FailureClass::BidiOk:
            return fallback;
        case FailureClass::TxBroken:
            return HealthState::Suspect;
        case FailureClass::RxBroken:
            return HealthState::Suspect;
        case FailureClass::BidiFailed:
            return HealthState::Quarantined;
        case FailureClass::Inconclusive:
            return fallback;
        default:
            return fallback;
    }
}

template <class Ctx, std::size_t MaxPeers, std::size_t Window, std::size_t MaxWitnesses>
    requires CtxFitsAsymmetricFailureMint<Ctx>
[[nodiscard]] constexpr AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>
mint_asymmetric_failure_detector(Ctx const&, AsymmetricFailurePolicy policy) noexcept {
    return AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>{policy};
}

}  // namespace crucible::topology
