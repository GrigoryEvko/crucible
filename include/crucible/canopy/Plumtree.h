#pragma once

// Bounded Plumtree broadcast substrate for Canopy.
//
// This layer owns eager/lazy link classification, bounded message-ID history,
// and repair-plan construction. Transport owns bytes, timers, and channels:
// callers carry Plumtree plans over HyParView/CNTP/Scuttlebutt surfaces.

#include <crucible/Platform.h>
#include <crucible/canopy/HyParView.h>
#include <crucible/cntp/Integrity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::canopy {

template <std::size_t MaxPeers, std::size_t MaxHistory>
concept PlumtreeShape =
    MaxPeers > 0 &&
    MaxHistory > 0 &&
    MaxPeers <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max()) &&
    MaxHistory <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires (Capacity > 0 &&
              Capacity <= static_cast<std::size_t>(
                  std::numeric_limits<std::uint16_t>::max()))
using PlumtreeCount =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity)>,
                    std::uint16_t>;

using PlumtreeDurationNs =
    safety::Refined<safety::positive, std::uint64_t>;
using PlumtreePositiveCount =
    safety::Refined<safety::positive, std::uint16_t>;
using PlumtreePayloadBytes =
    safety::Refined<safety::positive, std::uint32_t>;
using PlumtreeMessageId =
    safety::Tagged<cntp::IntegrityHash, safety::source::Plumtree>;
using PlumtreeMessageHash = std::uint64_t;

enum class PlumtreeLinkState : std::uint8_t {
    Eager,
    Lazy,
};

enum class PlumtreeReceiveKind : std::uint8_t {
    FirstSeen,
    Duplicate,
};

enum class PlumtreeError : std::uint8_t {
    CapacityExceeded,
    DuplicatePeer,
    EmptyMessage,
    InvalidConfig,
    PeerNotFound,
    UnknownPeer,
    ZeroUuid,
};

[[nodiscard]] std::string_view
plumtree_error_name(PlumtreeError error) noexcept;

struct PlumtreeConfig {
    PlumtreeDurationNs ihave_timeout_ns{100'000'000ULL};
    PlumtreeDurationNs repair_timeout_ns{200'000'000ULL};
    PlumtreeDurationNs lazy_push_period_ns{100'000'000ULL};
    PlumtreePositiveCount max_eager_fanout{5};
};

struct PlumtreeMessage {
    PlumtreeMessageHash id_hash = 0;
    std::uint32_t payload_bytes = 0;
};

using GossipedPlumtreeMessage =
    safety::Tagged<PlumtreeMessage, safety::source::Gossiped>;

template <std::size_t MaxHistory>
    requires (MaxHistory > 0)
struct PlumtreeIHave {
    safety::FixedArray<PlumtreeMessageHash, MaxHistory> ids{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr PlumtreeCount<MaxHistory>
    size() const noexcept {
        return PlumtreeCount<MaxHistory>{
            count,
            typename PlumtreeCount<MaxHistory>::Trusted{}};
    }
};

template <std::size_t MaxHistory>
    requires (MaxHistory > 0)
using GossipedPlumtreeIHave =
    safety::Tagged<PlumtreeIHave<MaxHistory>, safety::source::Gossiped>;

template <std::size_t MaxPeers, std::size_t MaxHistory>
    requires PlumtreeShape<MaxPeers, MaxHistory>
struct PlumtreeBroadcastPlan {
    PlumtreeMessage message;
    safety::FixedArray<cog::CogIdentity, MaxPeers> eager_peers{};
    safety::FixedArray<cog::CogIdentity, MaxPeers> lazy_peers{};
    std::uint16_t eager_count = 0;
    std::uint16_t lazy_count = 0;

    [[nodiscard]] constexpr PlumtreeCount<MaxPeers>
    eager_size() const noexcept {
        return PlumtreeCount<MaxPeers>{
            eager_count,
            typename PlumtreeCount<MaxPeers>::Trusted{}};
    }

    [[nodiscard]] constexpr PlumtreeCount<MaxPeers>
    lazy_size() const noexcept {
        return PlumtreeCount<MaxPeers>{
            lazy_count,
            typename PlumtreeCount<MaxPeers>::Trusted{}};
    }
};

template <std::size_t MaxPeers, std::size_t MaxHistory>
    requires PlumtreeShape<MaxPeers, MaxHistory>
struct PlumtreeReceivePlan {
    PlumtreeReceiveKind kind = PlumtreeReceiveKind::FirstSeen;
    PlumtreeBroadcastPlan<MaxPeers, MaxHistory> forward{};
};

template <std::size_t MaxHistory>
    requires (MaxHistory > 0)
struct PlumtreeRepairPlan {
    safety::FixedArray<PlumtreeMessageHash, MaxHistory> requested{};
    std::uint16_t count = 0;
    cog::CogIdentity source{};

    [[nodiscard]] constexpr PlumtreeCount<MaxHistory>
    size() const noexcept {
        return PlumtreeCount<MaxHistory>{
            count,
            typename PlumtreeCount<MaxHistory>::Trusted{}};
    }
};

[[nodiscard]] inline std::expected<PlumtreeMessageId, PlumtreeError>
plumtree_message_id(std::span<const std::byte> payload) noexcept {
    if (payload.empty()) {
        return std::unexpected(PlumtreeError::EmptyMessage);
    }
    auto id = cntp::xxhash64(payload);
    if (!id) {
        return std::unexpected(PlumtreeError::EmptyMessage);
    }
    return PlumtreeMessageId{*id};
}

[[nodiscard]] constexpr PlumtreeMessageHash
plumtree_message_hash(PlumtreeMessageId id) noexcept {
    return id.value().value();
}

template <std::size_t MaxPeers = 128, std::size_t MaxHistory = 1024>
    requires PlumtreeShape<MaxPeers, MaxHistory>
class alignas(64) PlumtreeBroadcast
    : public safety::Pinned<PlumtreeBroadcast<MaxPeers, MaxHistory>> {
public:
    using broadcast_plan_type = PlumtreeBroadcastPlan<MaxPeers, MaxHistory>;
    using receive_plan_type = PlumtreeReceivePlan<MaxPeers, MaxHistory>;
    using repair_plan_type = PlumtreeRepairPlan<MaxHistory>;
    using ihave_type = PlumtreeIHave<MaxHistory>;

    explicit PlumtreeBroadcast(PlumtreeConfig config = {}) noexcept
        : config_{config} {
        // FIXY-U-080 / fixy-A5-014: was __builtin_trap (silent SIGILL).
        CRUCIBLE_FATAL_INVARIANT(config_fits_shape_());
    }

    template <std::size_t HyMaxActive, std::size_t HyMaxPassive>
        requires HyParViewShape<HyMaxActive, HyMaxPassive>
    explicit PlumtreeBroadcast(
        HyParViewMembership<HyMaxActive, HyMaxPassive> const& membership,
        PlumtreeConfig config = {}) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(config_fits_shape_());
        auto active = membership.active_view();
        for (cog::CogIdentity const& peer : active.as_span()) {
            CRUCIBLE_FATAL_INVARIANT(
                add_link_(peer, PlumtreeLinkState::Eager).has_value());
        }
    }

    [[nodiscard]] PlumtreeConfig config() const noexcept {
        return config_;
    }

    [[nodiscard]] PlumtreeCount<MaxPeers>
    link_count() const noexcept {
        return PlumtreeCount<MaxPeers>{
            link_count_,
            typename PlumtreeCount<MaxPeers>::Trusted{}};
    }

    [[nodiscard]] PlumtreeCount<MaxPeers>
    eager_count() const noexcept {
        return PlumtreeCount<MaxPeers>{
            eager_count_,
            typename PlumtreeCount<MaxPeers>::Trusted{}};
    }

    [[nodiscard]] PlumtreeCount<MaxPeers>
    lazy_count() const noexcept {
        return PlumtreeCount<MaxPeers>{
            static_cast<std::uint16_t>(link_count_ - eager_count_),
            typename PlumtreeCount<MaxPeers>::Trusted{}};
    }

    [[nodiscard]] PlumtreeCount<MaxHistory>
    history_size() const noexcept {
        return PlumtreeCount<MaxHistory>{
            history_count_,
            typename PlumtreeCount<MaxHistory>::Trusted{}};
    }

    [[nodiscard]] std::expected<void, PlumtreeError>
    add_eager_peer(HyParViewPeer peer) noexcept {
        return add_link_(peer.value(), PlumtreeLinkState::Eager);
    }

    [[nodiscard]] std::expected<void, PlumtreeError>
    add_lazy_peer(HyParViewPeer peer) noexcept {
        return add_link_(peer.value(), PlumtreeLinkState::Lazy);
    }

    [[nodiscard]] std::expected<PlumtreeLinkState, PlumtreeError>
    link_state(cog::Uuid peer) const noexcept {
        auto idx = find_link_(peer);
        if (!idx) {
            return std::unexpected(PlumtreeError::PeerNotFound);
        }
        return links_[*idx].state;
    }

    [[nodiscard]] std::expected<broadcast_plan_type, PlumtreeError>
    publish(std::span<const std::byte> payload) noexcept {
        auto message = make_message_(payload);
        if (!message) {
            return std::unexpected(message.error());
        }
        remember_(message->id_hash);
        return build_broadcast_plan_(*message, cog::Uuid{});
    }

    [[nodiscard]] std::expected<receive_plan_type, PlumtreeError>
    receive_message(HyParViewPeer from,
                    GossipedPlumtreeMessage message) noexcept {
        auto peer_idx = find_link_(from.value().uuid);
        if (!peer_idx) {
            return std::unexpected(PlumtreeError::UnknownPeer);
        }
        PlumtreeMessage const& incoming = message.value();
        if (incoming.id_hash == 0 || incoming.payload_bytes == 0) {
            return std::unexpected(PlumtreeError::EmptyMessage);
        }

        receive_plan_type out{};
        if (seen_(incoming.id_hash)) {
            out.kind = PlumtreeReceiveKind::Duplicate;
            if (links_[*peer_idx].state == PlumtreeLinkState::Eager) {
                links_[*peer_idx].state = PlumtreeLinkState::Lazy;
                --eager_count_;
            }
            out.forward.message = incoming;
            return out;
        }

        promote_eager_(*peer_idx);
        remember_(incoming.id_hash);
        out.kind = PlumtreeReceiveKind::FirstSeen;
        out.forward = build_broadcast_plan_(incoming, from.value().uuid);
        return out;
    }

    [[nodiscard]] std::expected<repair_plan_type, PlumtreeError>
    receive_ihave(HyParViewPeer from,
                  GossipedPlumtreeIHave<MaxHistory> ihave) noexcept {
        auto peer_idx = find_link_(from.value().uuid);
        if (!peer_idx) {
            return std::unexpected(PlumtreeError::UnknownPeer);
        }

        repair_plan_type out{.source = from.value()};
        PlumtreeIHave<MaxHistory> const& raw = ihave.value();
        if (raw.count > MaxHistory) {
            return std::unexpected(PlumtreeError::InvalidConfig);
        }
        for (std::uint16_t i = 0; i < raw.count; ++i) {
            PlumtreeMessageHash id = raw.ids[i];
            if (id == 0) {
                return std::unexpected(PlumtreeError::EmptyMessage);
            }
            if (seen_(id)) {
                continue;
            }
            out.requested[out.count] = id;
            ++out.count;
        }
        if (out.count != 0) {
            promote_eager_(*peer_idx);
        }
        return out;
    }

    [[nodiscard]] ihave_type ihave_summary() const noexcept {
        ihave_type out{};
        for (std::uint16_t i = 0; i < history_count_; ++i) {
            const std::uint16_t idx = static_cast<std::uint16_t>(
                (static_cast<std::size_t>(history_cursor_) + MaxHistory -
                 static_cast<std::size_t>(history_count_) +
                 static_cast<std::size_t>(i)) %
                MaxHistory);
            out.ids[out.count] = history_[idx];
            ++out.count;
        }
        return out;
    }

private:
    struct LinkSlot {
        bool occupied = false;
        cog::CogIdentity peer{};
        PlumtreeLinkState state = PlumtreeLinkState::Lazy;
    };

    [[nodiscard]] bool config_fits_shape_() const noexcept {
        return config_.max_eager_fanout.value() <= MaxPeers;
    }

    [[nodiscard]] std::expected<PlumtreeMessage, PlumtreeError>
    make_message_(std::span<const std::byte> payload) const noexcept {
        if (payload.empty()) {
            return std::unexpected(PlumtreeError::EmptyMessage);
        }
        if (payload.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected(PlumtreeError::InvalidConfig);
        }
        auto id = plumtree_message_id(payload);
        if (!id) {
            return std::unexpected(id.error());
        }
        return PlumtreeMessage{
            .id_hash = plumtree_message_hash(*id),
            .payload_bytes = static_cast<std::uint32_t>(payload.size()),
        };
    }

    [[nodiscard]] std::expected<void, PlumtreeError>
    add_link_(cog::CogIdentity peer, PlumtreeLinkState state) noexcept {
        if (peer.uuid.is_zero()) {
            return std::unexpected(PlumtreeError::ZeroUuid);
        }
        if (find_link_(peer.uuid).has_value()) {
            return std::unexpected(PlumtreeError::DuplicatePeer);
        }
        if (link_count_ == MaxPeers) {
            return std::unexpected(PlumtreeError::CapacityExceeded);
        }
        if (state == PlumtreeLinkState::Eager &&
            eager_count_ == config_.max_eager_fanout.value()) {
            state = PlumtreeLinkState::Lazy;
        }
        links_[link_count_] = LinkSlot{
            .occupied = true,
            .peer = peer,
            .state = state,
        };
        ++link_count_;
        if (state == PlumtreeLinkState::Eager) {
            ++eager_count_;
        }
        return {};
    }

    [[nodiscard]] std::expected<std::uint16_t, PlumtreeError>
    find_link_(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < link_count_; ++i) {
            if (links_[i].occupied && links_[i].peer.uuid == peer) {
                return i;
            }
        }
        return std::unexpected(PlumtreeError::PeerNotFound);
    }

    void promote_eager_(std::uint16_t idx) noexcept {
        if (links_[idx].state == PlumtreeLinkState::Eager) {
            return;
        }
        if (eager_count_ == config_.max_eager_fanout.value()) {
            for (std::uint16_t i = 0; i < link_count_; ++i) {
                if (i != idx &&
                    links_[i].state == PlumtreeLinkState::Eager) {
                    links_[i].state = PlumtreeLinkState::Lazy;
                    --eager_count_;
                    break;
                }
            }
        }
        if (eager_count_ == config_.max_eager_fanout.value()) {
            return;
        }
        links_[idx].state = PlumtreeLinkState::Eager;
        ++eager_count_;
    }

    [[nodiscard]] bool seen_(PlumtreeMessageHash id) const noexcept {
        for (std::uint16_t i = 0; i < history_count_; ++i) {
            if (history_[i] == id) {
                return true;
            }
        }
        return false;
    }

    void remember_(PlumtreeMessageHash id) noexcept {
        if (seen_(id)) {
            return;
        }
        history_[history_cursor_] = id;
        history_cursor_ = static_cast<std::uint16_t>(
            (static_cast<std::size_t>(history_cursor_) + std::size_t{1}) %
            MaxHistory);
        if (history_count_ < MaxHistory) {
            ++history_count_;
        }
    }

    [[nodiscard]] broadcast_plan_type
    build_broadcast_plan_(PlumtreeMessage message,
                          cog::Uuid except) const noexcept {
        broadcast_plan_type out{.message = message};
        for (std::uint16_t i = 0; i < link_count_; ++i) {
            LinkSlot const& link = links_[i];
            if (!link.occupied || link.peer.uuid == except) {
                continue;
            }
            if (link.state == PlumtreeLinkState::Eager) {
                out.eager_peers[out.eager_count] = link.peer;
                ++out.eager_count;
            } else {
                out.lazy_peers[out.lazy_count] = link.peer;
                ++out.lazy_count;
            }
        }
        return out;
    }

    PlumtreeConfig config_{};
    safety::FixedArray<LinkSlot, MaxPeers> links_{};
    safety::FixedArray<PlumtreeMessageHash, MaxHistory> history_{};
    std::uint16_t link_count_ = 0;
    std::uint16_t eager_count_ = 0;
    std::uint16_t history_count_ = 0;
    std::uint16_t history_cursor_ = 0;
};

static_assert(!std::is_copy_constructible_v<PlumtreeBroadcast<4, 8>>);
static_assert(!std::is_move_constructible_v<PlumtreeBroadcast<4, 8>>);

template <std::size_t MaxPeers = 128,
          std::size_t MaxHistory = 1024,
          std::size_t HyMaxActive,
          std::size_t HyMaxPassive>
    requires PlumtreeShape<MaxPeers, MaxHistory> &&
             HyParViewShape<HyMaxActive, HyMaxPassive>
[[nodiscard]] PlumtreeBroadcast<MaxPeers, MaxHistory>
mint_plumtree(
    effects::Init,
    HyParViewMembership<HyMaxActive, HyMaxPassive> const& membership,
    PlumtreeConfig config = {}) noexcept {
    return PlumtreeBroadcast<MaxPeers, MaxHistory>{membership, config};
}

}  // namespace crucible::canopy
