#pragma once

#include <crucible/Platform.h>
#include <crucible/canopy/SlotTable.h>
#include <crucible/canopy/Swim.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::canopy {

// The peer, piggyback and event tables are slot tables, and the round-trip
// window holds at least two samples.
template <std::size_t MaxPeers, std::size_t MaxPiggyback, std::size_t RttWindow, std::size_t MaxEvents>
concept LifeguardShape =
    SwimShape<MaxPeers, MaxPiggyback> && SlotCapacity<MaxEvents> && SlotCapacity<RttWindow> && RttWindow > 1;

using LifeguardDurationNs = ::fixy::Refined<::fixy::positive, std::uint64_t>;
using LifeguardPositiveCount = ::fixy::Refined<::fixy::positive, std::uint16_t>;
using LifeguardMultiplier = ::fixy::Refined<::fixy::positive, std::uint16_t>;
using LifeguardRttNs = ::fixy::Refined<::fixy::positive, std::uint64_t>;

enum class LifeguardOutcome : std::uint8_t {
    Ack,
    Timeout,
    IndirectAck,
    IndirectTimeout,
    NoLossWindow,
};

enum class LifeguardError : std::uint8_t {
    CapacityExceeded,
    DuplicatePeer,
    InvalidConfig,
    PeerNotFound,
    ZeroUuid,
};

[[nodiscard]] std::string_view lifeguard_error_name(LifeguardError error) noexcept;

struct LifeguardConfig {
    LifeguardDurationNs base_probe_timeout_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{500000000ULL});
    LifeguardPositiveCount min_indirect_checks = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
    LifeguardPositiveCount max_indirect_checks = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{5});
    LifeguardMultiplier min_lhm = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
    LifeguardMultiplier max_lhm = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{8});
    LifeguardPositiveCount lhm_timeout_penalty = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
    LifeguardPositiveCount lhm_success_recovery = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
    LifeguardPositiveCount rtt_safety_multiplier = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{3});
};

struct LifeguardEvent {
    cog::CogIdentity peer{};
    LifeguardOutcome outcome = LifeguardOutcome::Ack;
    std::uint16_t prior_lhm = 1;
    std::uint16_t next_lhm = 1;
    std::uint64_t observed_rtt_ns = 0;
    std::uint64_t sequence = 0;
};

struct LifeguardProbe {
    cog::Uuid target{};
    std::uint64_t sequence = 0;
    std::uint64_t deadline_ns = 0;
    std::uint64_t timeout_ns = 0;
    std::uint16_t indirect_checks = 0;
};

struct LifeguardRefutePlan {
    bool should_refute = false;
    SwimEvent alive{};
};

// The multiplier events that the next drain hands to the caller.
template <std::size_t Capacity>
    requires SlotCapacity<Capacity>
struct LifeguardEventBatch : SlotTable<LifeguardEvent, Capacity> {};

template <std::size_t MaxPeers, std::size_t MaxPiggyback, std::size_t RttWindow, std::size_t MaxEvents>
    requires LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
class LifeguardSwim;

// The one door: a Lifeguard holds the failure-detector state of a process,
// so only a context that owns Init builds one.
template <std::size_t MaxPeers = 128, std::size_t MaxPiggyback = 32, std::size_t RttWindow = 16,
          std::size_t MaxEvents = MaxPeers * 4>
    requires LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
[[nodiscard]] constexpr LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
mint_lifeguard_swim(::foundation::effects::Init init, SwimPeer local_peer, std::span<const SwimPeer> initial_peers = {},
                    LifeguardConfig lifeguard_config = {}, SwimConfig swim_config = {}) noexcept;

template <std::size_t MaxPeers = 128, std::size_t MaxPiggyback = 32, std::size_t RttWindow = 16,
          std::size_t MaxEvents = MaxPeers * 4>
    requires LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
class alignas(64) LifeguardSwim
    : public ::foundation::Pinned<LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>> {
public:
    using swim_type = SwimMembership<MaxPeers, MaxPiggyback>;
    using witness_set_type = SwimWitnessSet<MaxPeers>;
    using event_batch_type = LifeguardEventBatch<MaxEvents>;

    [[nodiscard]] constexpr LifeguardConfig lifeguard_config() const noexcept { return lifeguard_config_; }

    [[nodiscard]] constexpr SwimConfig swim_config() const noexcept { return swim_.config(); }

    [[nodiscard]] constexpr cog::CogIdentity local_peer() const noexcept { return local_; }

    [[nodiscard]] constexpr BoundedSlotCount<MaxPeers> size() const noexcept { return swim_.size(); }

    [[nodiscard]] constexpr std::expected<void, LifeguardError> add_peer(SwimPeer peer) noexcept {
        auto added = swim_.add_peer(peer);
        if (!added) {
            return std::unexpected(map_swim_error_(added.error()));
        }
        if (!ensure_slot_(peer.value())) {
            return std::unexpected(LifeguardError::CapacityExceeded);
        }
        return {};
    }

    [[nodiscard]] std::expected<LifeguardMultiplier, LifeguardError>
    local_health_multiplier(cog::Uuid peer) const noexcept {
        Slot const* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        // The multiplier is clamped to [min_lhm, max_lhm], and min_lhm is positive.
        return ::fixy::mint_refined_trusted<::fixy::positive>(slot->lhm);
    }

    [[nodiscard]] std::expected<LifeguardDurationNs, LifeguardError> adaptive_timeout(cog::Uuid peer) const noexcept {
        Slot const* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        const std::uint64_t by_lhm = sat_mul_(lifeguard_config_.base_probe_timeout_ns.value(), slot->lhm);
        const std::uint64_t by_rtt = slot->rtt_count == 0
                                       ? std::uint64_t{0}
                                       : sat_mul_(mean_rtt_(*slot), lifeguard_config_.rtt_safety_multiplier.value());
        const std::uint64_t timeout = std::max(by_lhm, by_rtt);
        return ::fixy::mint_refined_trusted<::fixy::positive>(timeout == 0 ? std::uint64_t{1} : timeout);
    }

    [[nodiscard]] std::optional<LifeguardProbe> next_probe(std::uint64_t now_ns) noexcept {
        auto probe = swim_.next_probe(now_ns);
        if (!probe) {
            return std::nullopt;
        }
        auto timeout = adaptive_timeout(probe->target);
        if (!timeout) [[unlikely]] {
            return std::nullopt;
        }
        const std::uint64_t timeout_ns = timeout->value();
        return LifeguardProbe{
            .target = probe->target,
            .sequence = probe->sequence,
            .deadline_ns = sat_add_(now_ns, timeout_ns),
            .timeout_ns = timeout_ns,
            .indirect_checks = adaptive_indirect_count_(probe->target),
        };
    }

    [[nodiscard]] std::expected<void, LifeguardError> on_ack(cog::Uuid peer, std::uint64_t now_ns,
                                                             LifeguardRttNs rtt) noexcept {
        auto ack = swim_.on_ack(peer, now_ns);
        if (!ack) {
            return std::unexpected(map_swim_error_(ack.error()));
        }
        Slot* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        record_rtt_(*slot, rtt.value());
        recover_lhm_(*slot, LifeguardOutcome::Ack, rtt.value());
        return {};
    }

    [[nodiscard]] std::expected<void, LifeguardError> on_ping_timeout(cog::Uuid peer) noexcept {
        auto timeout = swim_.on_ping_timeout(peer);
        if (!timeout) {
            return std::unexpected(map_swim_error_(timeout.error()));
        }
        Slot* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        penalize_lhm_(*slot, LifeguardOutcome::Timeout);
        return {};
    }

    [[nodiscard]] std::expected<void, LifeguardError> on_indirect_ack(cog::Uuid peer, std::uint64_t now_ns,
                                                                      LifeguardRttNs rtt) noexcept {
        auto ack = swim_.on_indirect_ack(peer, now_ns);
        if (!ack) {
            return std::unexpected(map_swim_error_(ack.error()));
        }
        Slot* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        record_rtt_(*slot, rtt.value());
        recover_lhm_(*slot, LifeguardOutcome::IndirectAck, rtt.value());
        return {};
    }

    [[nodiscard]] std::expected<void, LifeguardError> on_indirect_timeout(cog::Uuid peer) noexcept {
        auto timeout = swim_.on_indirect_timeout(peer);
        if (!timeout) {
            return std::unexpected(map_swim_error_(timeout.error()));
        }
        Slot* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        penalize_lhm_(*slot, LifeguardOutcome::IndirectTimeout);
        return {};
    }

    [[nodiscard]] std::expected<void, LifeguardError> on_no_loss_window(cog::Uuid peer) noexcept {
        Slot* slot = find_slot_(peer);
        if (slot == nullptr) {
            return std::unexpected(LifeguardError::PeerNotFound);
        }
        recover_lhm_(*slot, LifeguardOutcome::NoLossWindow, 0);
        return {};
    }

    [[nodiscard]] witness_set_type indirect_witnesses(cog::Uuid suspect) const noexcept {
        witness_set_type out{};
        const std::uint16_t limit = adaptive_indirect_count_(suspect);
        auto live = swim_.live_peers();
        for (std::size_t i = 0; i < live.size() && out.count < limit; ++i) {
            if (live[i].uuid == suspect) {
                continue;
            }
            // The set has MaxPeers slots, and the live view holds at most
            // MaxPeers peers.
            (void)out.push(live[i].uuid);
        }
        return out;
    }

    [[nodiscard]] std::expected<LifeguardRefutePlan, LifeguardError> apply_gossip(GossipedSwimEvent event,
                                                                                  std::uint64_t now_ns) noexcept {
        SwimEvent const& incoming = event.value();
        if (incoming.peer.uuid.is_zero()) {
            return std::unexpected(LifeguardError::ZeroUuid);
        }
        if (incoming.peer.uuid == local_.uuid && incoming.state == SwimState::Suspect) {
            LifeguardRefutePlan out{
                .should_refute = true,
                .alive =
                    SwimEvent{
                        .peer = local_,
                        .state = SwimState::Alive,
                        .consecutive_misses = 0,
                        .incarnation = sat_add_(incoming.incarnation, 1),
                        .sequence = ++sequence_,
                    },
            };
            return out;
        }
        auto applied = swim_.apply_gossip(event, now_ns);
        if (!applied) {
            return std::unexpected(map_swim_error_(applied.error()));
        }
        if (!ensure_slot_(incoming.peer)) {
            return std::unexpected(LifeguardError::CapacityExceeded);
        }
        if (incoming.state == SwimState::Suspect || incoming.state == SwimState::Dead) {
            Slot* slot = find_slot_(incoming.peer.uuid);
            if (slot != nullptr) {
                penalize_lhm_(*slot, LifeguardOutcome::Timeout);
            }
        }
        return LifeguardRefutePlan{};
    }

    [[nodiscard]] event_batch_type event_batch() const noexcept {
        event_batch_type out{};
        for (std::uint16_t i = 0; i < event_count_; ++i) {
            // The batch and the event queue have the same MaxEvents slots.
            (void)out.push(events_[i]);
        }
        return out;
    }

    void acknowledge_events(std::uint16_t count) noexcept {
        const std::uint16_t n = std::min(count, event_count_);
        for (std::uint16_t i = n; i < event_count_; ++i) {
            events_[static_cast<std::uint16_t>(i - n)] = events_[i];
        }
        event_count_ = static_cast<std::uint16_t>(event_count_ - n);
    }

    [[nodiscard]] swim_type const& swim() const noexcept { return swim_; }

private:
    // The embedded membership comes from its own mint, under the Init
    // context that the Lifeguard mint forwards.
    constexpr LifeguardSwim(::foundation::effects::Init init, SwimPeer local_peer,
                            std::span<const SwimPeer> initial_peers, LifeguardConfig lifeguard_config,
                            SwimConfig swim_config) noexcept
        : swim_(mint_swim_membership<MaxPeers, MaxPiggyback>(init, {}, swim_config)),
          local_{local_peer.value()},
          lifeguard_config_{lifeguard_config} {
        CRUCIBLE_FATAL_INVARIANT(config_valid_());
        for (SwimPeer const& peer : initial_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_peer(peer).has_value());
        }
    }

    template <std::size_t P, std::size_t Q, std::size_t R, std::size_t E>
        requires LifeguardShape<P, Q, R, E>
    friend constexpr LifeguardSwim<P, Q, R, E> mint_lifeguard_swim(::foundation::effects::Init, SwimPeer,
                                                                   std::span<const SwimPeer>, LifeguardConfig,
                                                                   SwimConfig) noexcept;

    // Slots are dense and a peer never leaves its slot, so the live slots
    // are [0, slot_count_).
    struct alignas(64) Slot {
        cog::CogIdentity peer{};
        std::uint16_t lhm = 1;
        ::fixy::FixedArray<std::uint64_t, RttWindow> rtt_ns{};
        std::uint16_t rtt_count = 0;
        std::uint16_t rtt_cursor = 0;
    };

    [[nodiscard]] constexpr bool config_valid_() const noexcept {
        return !local_.uuid.is_zero() && lifeguard_config_.min_lhm.value() <= lifeguard_config_.max_lhm.value()
            && lifeguard_config_.min_indirect_checks.value() <= lifeguard_config_.max_indirect_checks.value()
            && lifeguard_config_.max_indirect_checks.value() <= MaxPeers;
    }

    [[nodiscard]] static constexpr LifeguardError map_swim_error_(SwimError error) noexcept {
        switch (error) {
            case SwimError::CapacityExceeded:
                return LifeguardError::CapacityExceeded;
            case SwimError::DuplicatePeer:
                return LifeguardError::DuplicatePeer;
            case SwimError::PeerNotFound:
                return LifeguardError::PeerNotFound;
            case SwimError::ZeroUuid:
                return LifeguardError::ZeroUuid;
            default:
                return LifeguardError::InvalidConfig;
        }
    }

    [[nodiscard]] static constexpr std::uint64_t sat_add_(std::uint64_t a, std::uint64_t b) noexcept {
        const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
        return a > max - b ? max : a + b;
    }

    [[nodiscard]] static constexpr std::uint64_t sat_mul_(std::uint64_t a, std::uint64_t b) noexcept {
        if (a == 0 || b == 0) {
            return 0;
        }
        const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
        return a > max / b ? max : a * b;
    }

    // A Slot* from a mutable LifeguardSwim, a Slot const* from a const one.
    [[nodiscard]] constexpr auto* find_slot_(this auto& self, cog::Uuid peer) noexcept {
        using slot_pointer = decltype(&self.slots_[0]);
        for (std::uint16_t i = 0; i < self.slot_count_; ++i) {
            if (self.slots_[i].peer.uuid == peer) {
                return slot_pointer{&self.slots_[i]};
            }
        }
        return slot_pointer{nullptr};
    }

    [[nodiscard]] constexpr bool ensure_slot_(cog::CogIdentity peer) noexcept {
        if (peer.uuid.is_zero()) {
            return false;
        }
        if (find_slot_(peer.uuid) != nullptr) {
            return true;
        }
        const auto index = slot_count_.reserve_next();
        if (!index) {
            return false;
        }
        slots_.at(*index) = Slot{
            .peer = peer,
            .lhm = lifeguard_config_.min_lhm.value(),
        };
        return true;
    }

    [[nodiscard]] std::uint64_t mean_rtt_(Slot const& slot) const noexcept {
        if (slot.rtt_count == 0) {
            return lifeguard_config_.base_probe_timeout_ns.value();
        }
        std::uint64_t total = 0;
        for (std::uint16_t i = 0; i < slot.rtt_count; ++i) {
            total = sat_add_(total, slot.rtt_ns[i]);
        }
        const std::uint64_t mean = total / slot.rtt_count;
        return mean == 0 ? std::uint64_t{1} : mean;
    }

    [[nodiscard]] std::uint16_t adaptive_indirect_count_(cog::Uuid peer) const noexcept {
        Slot const* slot = find_slot_(peer);
        const std::uint16_t lhm = slot == nullptr ? lifeguard_config_.min_lhm.value() : slot->lhm;
        const std::uint16_t lhm_delta = lhm <= lifeguard_config_.min_lhm.value()
                                          ? std::uint16_t{0}
                                          : static_cast<std::uint16_t>(lhm - lifeguard_config_.min_lhm.value());
        const std::uint16_t desired = static_cast<std::uint16_t>(
            lifeguard_config_.min_indirect_checks.value()
            + std::min<std::uint16_t>(lhm_delta,
                                      static_cast<std::uint16_t>(lifeguard_config_.max_indirect_checks.value()
                                                                 - lifeguard_config_.min_indirect_checks.value())));
        return std::min<std::uint16_t>(desired, lifeguard_config_.max_indirect_checks.value());
    }

    void record_rtt_(Slot& slot, std::uint64_t rtt_ns) noexcept {
        slot.rtt_ns[slot.rtt_cursor] = rtt_ns;
        slot.rtt_cursor =
            static_cast<std::uint16_t>((static_cast<std::size_t>(slot.rtt_cursor) + std::size_t{1}) % RttWindow);
        if (slot.rtt_count < RttWindow) {
            ++slot.rtt_count;
        }
    }

    void append_event_(Slot const& slot, LifeguardOutcome outcome, std::uint16_t prior_lhm,
                       std::uint64_t observed_rtt_ns) noexcept {
        LifeguardEvent event{
            .peer = slot.peer,
            .outcome = outcome,
            .prior_lhm = prior_lhm,
            .next_lhm = slot.lhm,
            .observed_rtt_ns = observed_rtt_ns,
            .sequence = ++sequence_,
        };
        if (event_count_ == MaxEvents) {
            for (std::uint16_t i = 1; i < event_count_; ++i) {
                events_[static_cast<std::uint16_t>(i - 1)] = events_[i];
            }
            --event_count_;
        }
        events_[event_count_] = event;
        ++event_count_;
    }

    void penalize_lhm_(Slot& slot, LifeguardOutcome outcome) noexcept {
        const std::uint16_t prior = slot.lhm;
        const std::uint16_t max_lhm = lifeguard_config_.max_lhm.value();
        const std::uint16_t penalty = lifeguard_config_.lhm_timeout_penalty.value();
        slot.lhm = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(max_lhm, static_cast<std::uint32_t>(slot.lhm) + penalty));
        append_event_(slot, outcome, prior, 0);
    }

    void recover_lhm_(Slot& slot, LifeguardOutcome outcome, std::uint64_t observed_rtt_ns) noexcept {
        const std::uint16_t prior = slot.lhm;
        const std::uint16_t min_lhm = lifeguard_config_.min_lhm.value();
        const std::uint16_t recovery = lifeguard_config_.lhm_success_recovery.value();
        slot.lhm = slot.lhm <= min_lhm || static_cast<std::uint16_t>(slot.lhm - min_lhm) <= recovery
                     ? min_lhm
                     : static_cast<std::uint16_t>(slot.lhm - recovery);
        append_event_(slot, outcome, prior, observed_rtt_ns);
    }

    swim_type swim_;
    cog::CogIdentity local_{};
    LifeguardConfig lifeguard_config_{};
    ::fixy::FixedArray<Slot, MaxPeers> slots_{};
    SlotCount<MaxPeers> slot_count_{};
    ::fixy::FixedArray<LifeguardEvent, MaxEvents> events_{};
    std::uint16_t event_count_ = 0;
    std::uint64_t sequence_ = 0;
};

static_assert(!std::is_constructible_v<LifeguardSwim<4, 8, 4, 8>, SwimPeer>);
static_assert(!std::is_copy_constructible_v<LifeguardSwim<4, 8, 4, 8>>);
static_assert(!std::is_move_constructible_v<LifeguardSwim<4, 8, 4, 8>>);

template <std::size_t MaxPeers, std::size_t MaxPiggyback, std::size_t RttWindow, std::size_t MaxEvents>
    requires LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
[[nodiscard]] constexpr LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>
mint_lifeguard_swim(::foundation::effects::Init init, SwimPeer local_peer, std::span<const SwimPeer> initial_peers,
                    LifeguardConfig lifeguard_config, SwimConfig swim_config) noexcept {
    return LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>{init, local_peer, initial_peers,
                                                                       lifeguard_config, swim_config};
}

}  // namespace crucible::canopy
