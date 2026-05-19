#pragma once

// Bounded SWIM membership substrate for Canopy.
//
// This header owns the protocol state machine and piggyback event
// surface.  Socket I/O is deliberately outside this layer: transports
// consume SwimProbe / SwimEvent values and feed acks, timeouts, and
// gossiped events back through the typed API here.

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace crucible::canopy {

template <std::size_t Capacity>
concept SwimCapacity =
    Capacity > 0 &&
    Capacity <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires SwimCapacity<Capacity>
using SwimCount =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity)>,
                    std::uint16_t>;

template <std::size_t Capacity>
    requires SwimCapacity<Capacity>
using SwimIndex =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity - 1)>,
                    std::uint16_t>;

using SwimDurationNs = safety::Refined<safety::positive, std::uint64_t>;
using SwimPositiveCount = safety::Refined<safety::positive, std::uint16_t>;
using SwimPeer = safety::Tagged<cog::CogIdentity, safety::source::SwimMember>;

enum class SwimState : std::uint8_t {
    Alive = 0,
    Suspect = 1,
    Dead = 2,
};

enum class SwimError : std::uint8_t {
    CapacityExceeded,
    DuplicatePeer,
    PeerNotFound,
    ZeroUuid,
};

struct SwimConfig {
    SwimDurationNs period_ns{1'000'000'000ULL};
    SwimDurationNs ack_timeout_ns{500'000'000ULL};
    SwimDurationNs indirect_timeout_ns{500'000'000ULL};
    SwimPositiveCount indirect_checks{3};
    SwimPositiveCount suspicion_misses{2};
};

struct PeerHealth {
    SwimState state = SwimState::Dead;
    std::uint64_t last_heartbeat_ns = 0;
    std::uint32_t consecutive_misses = 0;
    std::uint64_t incarnation = 0;
};

struct SwimEvent {
    cog::CogIdentity peer{};
    SwimState state = SwimState::Alive;
    std::uint32_t consecutive_misses = 0;
    std::uint64_t incarnation = 0;
    std::uint64_t sequence = 0;
};

using GossipedSwimEvent =
    safety::Tagged<SwimEvent, safety::source::Gossiped>;

struct SwimProbe {
    cog::Uuid target{};
    std::uint64_t sequence = 0;
    std::uint64_t deadline_ns = 0;
};

template <std::size_t Capacity>
    requires SwimCapacity<Capacity>
struct SwimWitnessSet {
    safety::FixedArray<cog::Uuid, Capacity> peers{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr SwimCount<Capacity> size() const noexcept {
        return SwimCount<Capacity>{
            count,
            typename SwimCount<Capacity>::Trusted{}};
    }
};

template <std::size_t MaxPiggyback>
    requires SwimCapacity<MaxPiggyback>
struct SwimPiggybackBatch {
    safety::FixedArray<SwimEvent, MaxPiggyback> events{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr SwimCount<MaxPiggyback> size() const noexcept {
        return SwimCount<MaxPiggyback>{
            count,
            typename SwimCount<MaxPiggyback>::Trusted{}};
    }
};

template <std::size_t MaxPeers = 128, std::size_t MaxPiggyback = 32>
    requires SwimCapacity<MaxPeers> && SwimCapacity<MaxPiggyback>
class SwimMembership
    : public safety::Pinned<SwimMembership<MaxPeers, MaxPiggyback>> {
public:
    using peer_type = SwimPeer;
    using health_type = safety::Stale<PeerHealth>;
    using live_view_type =
        safety::Borrowed<const cog::CogIdentity, safety::source::SwimMember>;
    using witness_set_type = SwimWitnessSet<MaxPeers>;
    using piggyback_batch_type = SwimPiggybackBatch<MaxPiggyback>;

    explicit SwimMembership(SwimConfig config = {}) noexcept
        : config_{config} {}

    SwimMembership(
        SwimConfig config,
        std::span<const peer_type> initial_peers) noexcept
        : config_{config} {
        // FIXY-U-080 / fixy-A5-014: was __builtin_trap (silent SIGILL).
        for (peer_type const& peer : initial_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_peer(peer).has_value());
        }
    }

    [[nodiscard]] std::expected<void, SwimError>
    add_peer(peer_type peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(SwimError::ZeroUuid);
        }
        if (find_index_(id.uuid).has_value()) {
            return std::unexpected(SwimError::DuplicatePeer);
        }
        if (count_ == MaxPeers) {
            return std::unexpected(SwimError::CapacityExceeded);
        }

        PeerSlot& slot = slots_[count_];
        slot.occupied = true;
        slot.identity = id;
        slot.health = PeerHealth{
            .state = SwimState::Alive,
            .last_heartbeat_ns = 0,
            .consecutive_misses = 0,
            .incarnation = 1,
        };
        ++count_;
        append_event_(slot);
        return {};
    }

    [[nodiscard]] std::expected<void, SwimError>
    remove_peer(peer_type peer) noexcept {
        auto idx = find_index_(peer.value().uuid);
        if (!idx) {
            return std::unexpected(SwimError::PeerNotFound);
        }
        mark_dead_(*idx);
        return {};
    }

    [[nodiscard]] health_type
    health(cog::Uuid peer_id) const noexcept {
        auto idx = find_index_(peer_id);
        if (!idx) {
            return health_type::at_infinity(PeerHealth{});
        }
        PeerSlot const& slot = slots_[*idx];
        const std::uint64_t stale_rounds =
            sequence_ >= slot.last_seen_sequence
                ? sequence_ - slot.last_seen_sequence
                : std::uint64_t{0};
        return health_type::at(slot.health, stale_rounds);
    }

    [[nodiscard]] live_view_type live_peers() const noexcept {
        live_count_ = 0;
        for (std::uint16_t i = 0; i < count_; ++i) {
            PeerSlot const& slot = slots_[i];
            if (slot.occupied && slot.health.state != SwimState::Dead) {
                live_cache_[live_count_] = slot.identity;
                ++live_count_;
            }
        }
        return live_view_type{live_cache_.data(), live_count_};
    }

    [[nodiscard]] SwimCount<MaxPeers> size() const noexcept {
        return SwimCount<MaxPeers>{
            count_,
            typename SwimCount<MaxPeers>::Trusted{}};
    }

    [[nodiscard]] SwimConfig config() const noexcept {
        return config_;
    }

    [[nodiscard]] std::optional<SwimProbe>
    next_probe(std::uint64_t now_ns) noexcept {
        if (count_ == 0) {
            return std::nullopt;
        }
        for (std::uint16_t attempts = 0; attempts < count_; ++attempts) {
            const std::uint16_t idx =
                static_cast<std::uint16_t>((probe_cursor_ + attempts) % count_);
            PeerSlot const& slot = slots_[idx];
            if (!slot.occupied || slot.health.state == SwimState::Dead) {
                continue;
            }
            probe_cursor_ = static_cast<std::uint16_t>((idx + 1u) % count_);
            ++sequence_;
            return SwimProbe{
                .target = slot.identity.uuid,
                .sequence = sequence_,
                .deadline_ns = saturated_add_(
                    now_ns,
                    config_.ack_timeout_ns.value()),
            };
        }
        return std::nullopt;
    }

    [[nodiscard]] std::expected<void, SwimError>
    on_ack(cog::Uuid peer_id, std::uint64_t now_ns) noexcept {
        auto idx = find_index_(peer_id);
        if (!idx) {
            return std::unexpected(SwimError::PeerNotFound);
        }
        mark_alive_(*idx, now_ns);
        return {};
    }

    [[nodiscard]] std::expected<void, SwimError>
    on_ping_timeout(cog::Uuid peer_id) noexcept {
        auto idx = find_index_(peer_id);
        if (!idx) {
            return std::unexpected(SwimError::PeerNotFound);
        }
        miss_(*idx);
        return {};
    }

    [[nodiscard]] std::expected<void, SwimError>
    on_indirect_ack(cog::Uuid suspect, std::uint64_t now_ns) noexcept {
        return on_ack(suspect, now_ns);
    }

    [[nodiscard]] std::expected<void, SwimError>
    on_indirect_timeout(cog::Uuid suspect) noexcept {
        return on_ping_timeout(suspect);
    }

    [[nodiscard]] witness_set_type
    indirect_witnesses(cog::Uuid suspect) const noexcept {
        witness_set_type out{};
        const std::uint16_t limit = std::min<std::uint16_t>(
            config_.indirect_checks.value(),
            static_cast<std::uint16_t>(MaxPeers));
        for (std::uint16_t i = 0; i < count_ && out.count < limit; ++i) {
            PeerSlot const& slot = slots_[i];
            if (!slot.occupied ||
                slot.identity.uuid == suspect ||
                slot.health.state != SwimState::Alive) {
                continue;
            }
            out.peers[out.count] = slot.identity.uuid;
            ++out.count;
        }
        return out;
    }

    [[nodiscard]] piggyback_batch_type piggyback_batch() const noexcept {
        piggyback_batch_type out{};
        out.count = piggyback_count_;
        for (std::uint16_t i = 0; i < piggyback_count_; ++i) {
            out.events[i] = piggyback_[i];
        }
        return out;
    }

    void acknowledge_piggybacks(std::uint16_t count) noexcept {
        const std::uint16_t n = std::min(count, piggyback_count_);
        for (std::uint16_t i = n; i < piggyback_count_; ++i) {
            const auto dst = static_cast<std::size_t>(
                static_cast<std::uint16_t>(i - n));
            piggyback_[dst] = piggyback_[static_cast<std::size_t>(i)];
        }
        piggyback_count_ = static_cast<std::uint16_t>(piggyback_count_ - n);
    }

    [[nodiscard]] std::expected<void, SwimError>
    apply_gossip(GossipedSwimEvent event, std::uint64_t now_ns) noexcept {
        SwimEvent const& incoming = event.value();
        if (incoming.peer.uuid.is_zero()) {
            return std::unexpected(SwimError::ZeroUuid);
        }

        auto idx = find_index_(incoming.peer.uuid);
        if (!idx) {
            if (count_ == MaxPeers) {
                return std::unexpected(SwimError::CapacityExceeded);
            }
            PeerSlot& slot = slots_[count_];
            slot.occupied = true;
            slot.identity = incoming.peer;
            slot.health = PeerHealth{
                .state = incoming.state,
                .last_heartbeat_ns =
                    incoming.state == SwimState::Alive ? now_ns : 0,
                .consecutive_misses = incoming.consecutive_misses,
                .incarnation = incoming.incarnation,
            };
            slot.last_seen_sequence = sequence_;
            ++count_;
            append_event_(slot);
            return {};
        }

        PeerSlot& slot = slots_[*idx];
        if (!event_newer_(incoming, slot)) {
            return {};
        }
        slot.identity = incoming.peer;
        slot.health.state = incoming.state;
        slot.health.consecutive_misses = incoming.consecutive_misses;
        slot.health.incarnation = incoming.incarnation;
        if (incoming.state == SwimState::Alive) {
            slot.health.last_heartbeat_ns = now_ns;
        }
        slot.last_seen_sequence = sequence_;
        append_event_(slot);
        return {};
    }

private:
    struct alignas(64) PeerSlot {
        bool occupied = false;
        cog::CogIdentity identity{};
        PeerHealth health{};
        std::uint64_t last_seen_sequence = 0;
    };

    [[nodiscard]] static constexpr std::uint8_t
    state_rank_(SwimState state) noexcept {
        switch (state) {
            case SwimState::Alive:   return 0;
            case SwimState::Suspect: return 1;
            case SwimState::Dead:    return 2;
            default:                 return 2;
        }
    }

    [[nodiscard]] static constexpr std::uint64_t
    saturated_add_(std::uint64_t a, std::uint64_t b) noexcept {
        const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
        return a > max - b ? max : a + b;
    }

    [[nodiscard]] std::optional<std::uint16_t>
    find_index_(cog::Uuid peer_id) const noexcept {
        for (std::uint16_t i = 0; i < count_; ++i) {
            if (slots_[i].occupied && slots_[i].identity.uuid == peer_id) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool event_newer_(
        SwimEvent const& incoming,
        PeerSlot const& slot) noexcept {
        if (incoming.incarnation != slot.health.incarnation) {
            return incoming.incarnation > slot.health.incarnation;
        }
        return state_rank_(incoming.state) > state_rank_(slot.health.state);
    }

    void append_event_(PeerSlot const& slot) noexcept {
        SwimEvent event{
            .peer = slot.identity,
            .state = slot.health.state,
            .consecutive_misses = slot.health.consecutive_misses,
            .incarnation = slot.health.incarnation,
            .sequence = ++sequence_,
        };
        if (piggyback_count_ == MaxPiggyback) {
            for (std::uint16_t i = 1; i < piggyback_count_; ++i) {
                const auto dst = static_cast<std::size_t>(
                    static_cast<std::uint16_t>(i - std::uint16_t{1}));
                piggyback_[dst] = piggyback_[static_cast<std::size_t>(i)];
            }
            --piggyback_count_;
        }
        piggyback_[piggyback_count_] = event;
        ++piggyback_count_;
    }

    void mark_alive_(std::uint16_t idx, std::uint64_t now_ns) noexcept {
        PeerSlot& slot = slots_[idx];
        slot.health.state = SwimState::Alive;
        slot.health.last_heartbeat_ns = now_ns;
        slot.health.consecutive_misses = 0;
        ++slot.health.incarnation;
        slot.last_seen_sequence = sequence_;
        append_event_(slot);
    }

    void mark_dead_(std::uint16_t idx) noexcept {
        PeerSlot& slot = slots_[idx];
        if (slot.health.state == SwimState::Dead) {
            return;
        }
        slot.health.state = SwimState::Dead;
        ++slot.health.incarnation;
        slot.last_seen_sequence = sequence_;
        append_event_(slot);
    }

    void miss_(std::uint16_t idx) noexcept {
        PeerSlot& slot = slots_[idx];
        if (slot.health.state == SwimState::Dead) {
            return;
        }
        slot.health.consecutive_misses =
            slot.health.consecutive_misses ==
                    std::numeric_limits<std::uint32_t>::max()
                ? slot.health.consecutive_misses
                : slot.health.consecutive_misses + 1u;
        if (slot.health.consecutive_misses >=
            config_.suspicion_misses.value()) {
            slot.health.state = SwimState::Dead;
        } else {
            slot.health.state = SwimState::Suspect;
        }
        ++slot.health.incarnation;
        slot.last_seen_sequence = sequence_;
        append_event_(slot);
    }

    SwimConfig config_{};
    safety::FixedArray<PeerSlot, MaxPeers> slots_{};
    std::uint16_t count_ = 0;
    std::uint16_t probe_cursor_ = 0;
    std::uint64_t sequence_ = 0;
    safety::FixedArray<SwimEvent, MaxPiggyback> piggyback_{};
    std::uint16_t piggyback_count_ = 0;
    mutable safety::FixedArray<cog::CogIdentity, MaxPeers> live_cache_{};
    mutable std::uint16_t live_count_ = 0;
};

static_assert(!std::is_copy_constructible_v<SwimMembership<8>>);
static_assert(!std::is_move_constructible_v<SwimMembership<8>>);

[[nodiscard]] inline SwimPeer admit_swim_peer(cog::CogIdentity peer) noexcept {
    return SwimPeer{peer};
}

template <std::size_t MaxPeers = 128, std::size_t MaxPiggyback = 32>
    requires SwimCapacity<MaxPeers> && SwimCapacity<MaxPiggyback>
[[nodiscard]] SwimMembership<MaxPeers, MaxPiggyback>
mint_swim_membership(
    effects::Init,
    std::span<const SwimPeer> initial_peers = {},
    SwimConfig config = {}) noexcept {
    return SwimMembership<MaxPeers, MaxPiggyback>{config, initial_peers};
}

}  // namespace crucible::canopy
