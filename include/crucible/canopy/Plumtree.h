#pragma once

#include <crucible/canopy/HyParView.h>
#include <crucible/canopy/SlotTable.h>
#include <crucible/cntp/Integrity.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::canopy {

// The link table and the history ring are slot tables.
template <std::size_t MaxPeers, std::size_t MaxHistory>
concept PlumtreeShape = SlotCapacity<MaxPeers> && SlotCapacity<MaxHistory>;

// A broadcast tree built from an overlay has a link slot for each peer that
// the active view of the overlay can hold.  A tree with fewer slots would
// leave some active peers out of every broadcast.
template <std::size_t MaxPeers, std::size_t MaxHistory, std::size_t HyMaxActive, std::size_t HyMaxPassive>
concept PlumtreeFitsOverlay =
    PlumtreeShape<MaxPeers, MaxHistory> && HyParViewShape<HyMaxActive, HyMaxPassive> && HyMaxActive <= MaxPeers;

using PlumtreeDurationNs = ::fixy::Refined<::fixy::positive, std::uint64_t>;
using PlumtreePositiveCount = ::fixy::Refined<::fixy::positive, std::uint16_t>;
using PlumtreeMessageId = ::fixy::Tagged<cntp::IntegrityHash, ::fixy::tags::source::Plumtree>;
using PlumtreeMessageHash = std::uint64_t;

enum class PlumtreeLinkState : std::uint8_t {
    Eager,
    Lazy,
};

enum class PlumtreeReceiveKind : std::uint8_t {
    FirstSeen,
    Duplicate,
};

// foundation::reflect::enum_name gives the log spelling.
enum class PlumtreeError : std::uint8_t {
    CapacityExceeded,
    DuplicatePeer,
    EmptyMessage,
    InvalidConfig,
    PeerNotFound,
    UnknownPeer,
    ZeroUuid,
};

struct PlumtreeConfig {
    PlumtreeDurationNs ihave_timeout_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{100000000});
    PlumtreeDurationNs repair_timeout_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{200000000});
    PlumtreeDurationNs lazy_push_period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{100000000});
    PlumtreePositiveCount max_eager_fanout = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{5});
};

struct PlumtreeMessage {
    PlumtreeMessageHash id_hash = 0;
    std::uint32_t payload_bytes = 0;
};

using GossipedPlumtreeMessage = ::fixy::Tagged<PlumtreeMessage, ::fixy::tags::source::Gossiped>;

// The message ids of a repair summary, oldest first.
template <std::size_t MaxHistory>
    requires SlotCapacity<MaxHistory>
struct PlumtreeIHave : SlotTable<PlumtreeMessageHash, MaxHistory> {};

template <std::size_t MaxHistory>
    requires SlotCapacity<MaxHistory>
using GossipedPlumtreeIHave = ::fixy::Tagged<PlumtreeIHave<MaxHistory>, ::fixy::tags::source::Gossiped>;

template <std::size_t MaxPeers, std::size_t MaxHistory>
    requires PlumtreeShape<MaxPeers, MaxHistory>
struct PlumtreeBroadcastPlan {
    PlumtreeMessage message;
    SlotTable<cog::CogIdentity, MaxPeers> eager_peers{};
    SlotTable<cog::CogIdentity, MaxPeers> lazy_peers{};
};

template <std::size_t MaxPeers, std::size_t MaxHistory>
    requires PlumtreeShape<MaxPeers, MaxHistory>
struct PlumtreeReceivePlan {
    PlumtreeReceiveKind kind = PlumtreeReceiveKind::FirstSeen;
    PlumtreeBroadcastPlan<MaxPeers, MaxHistory> forward{};
};

template <std::size_t MaxHistory>
    requires SlotCapacity<MaxHistory>
struct PlumtreeRepairPlan {
    SlotTable<PlumtreeMessageHash, MaxHistory> requested{};
    cog::CogIdentity source{};
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
    return ::fixy::mint_tagged<::fixy::tags::source::Plumtree>(*id);
}

[[nodiscard]] constexpr PlumtreeMessageHash plumtree_message_hash(PlumtreeMessageId id) noexcept {
    return id.value().value();
}

// The recoverable check of a configuration: the eager fanout fits the link
// slots.  The mint does the same check and stops the process on a refusal.
template <std::size_t MaxPeers>
    requires SlotCapacity<MaxPeers>
[[nodiscard]] constexpr std::expected<PlumtreeConfig, PlumtreeError>
admit_plumtree_config(PlumtreeConfig config) noexcept {
    if (config.max_eager_fanout.value() > MaxPeers) {
        return std::unexpected(PlumtreeError::InvalidConfig);
    }
    return config;
}

template <std::size_t MaxPeers = 128, std::size_t MaxHistory = 1024>
    requires PlumtreeShape<MaxPeers, MaxHistory>
class PlumtreeBroadcast;

// The one door: a broadcast tree holds the dissemination state of a
// process, so only a context that owns Init builds one.  Each active peer
// of the overlay becomes an eager link, up to the eager fanout, and a lazy
// link after it.
template <std::size_t MaxPeers = 128, std::size_t MaxHistory = 1024, std::size_t HyMaxActive, std::size_t HyMaxPassive>
    requires PlumtreeFitsOverlay<MaxPeers, MaxHistory, HyMaxActive, HyMaxPassive>
[[nodiscard]] constexpr PlumtreeBroadcast<MaxPeers, MaxHistory>
mint_plumtree(::foundation::effects::Init, HyParViewMembership<HyMaxActive, HyMaxPassive> const& membership,
              PlumtreeConfig config = {}) noexcept;

template <std::size_t MaxPeers, std::size_t MaxHistory>
    requires PlumtreeShape<MaxPeers, MaxHistory>
class alignas(64) PlumtreeBroadcast : public ::foundation::Pinned<PlumtreeBroadcast<MaxPeers, MaxHistory>> {
public:
    using broadcast_plan_type = PlumtreeBroadcastPlan<MaxPeers, MaxHistory>;
    using receive_plan_type = PlumtreeReceivePlan<MaxPeers, MaxHistory>;
    using repair_plan_type = PlumtreeRepairPlan<MaxHistory>;
    using ihave_type = PlumtreeIHave<MaxHistory>;

    [[nodiscard]] constexpr PlumtreeConfig config() const noexcept { return config_; }

    [[nodiscard]] constexpr BoundedSlotCount<MaxPeers> link_count() const noexcept { return link_count_.bounded(); }

    // The eager links are a part of the links.
    [[nodiscard]] constexpr BoundedSlotCount<MaxPeers> eager_count() const noexcept {
        return ::fixy::mint_refined_trusted<slot_count_bound<MaxPeers>>(eager_count_);
    }

    [[nodiscard]] constexpr BoundedSlotCount<MaxPeers> lazy_count() const noexcept {
        return ::fixy::mint_refined_trusted<slot_count_bound<MaxPeers>>(
            static_cast<std::uint16_t>(link_count_.value() - eager_count_));
    }

    // remember_() stops the count at MaxHistory.
    [[nodiscard]] constexpr BoundedSlotCount<MaxHistory> history_size() const noexcept {
        return ::fixy::mint_refined_trusted<slot_count_bound<MaxHistory>>(history_count_);
    }

    [[nodiscard]] constexpr std::expected<void, PlumtreeError> add_eager_peer(HyParViewPeer peer) noexcept {
        return add_link_(peer.value(), PlumtreeLinkState::Eager);
    }

    [[nodiscard]] constexpr std::expected<void, PlumtreeError> add_lazy_peer(HyParViewPeer peer) noexcept {
        return add_link_(peer.value(), PlumtreeLinkState::Lazy);
    }

    [[nodiscard]] constexpr std::expected<PlumtreeLinkState, PlumtreeError> link_state(cog::Uuid peer) const noexcept {
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

    [[nodiscard]] constexpr std::expected<receive_plan_type, PlumtreeError>
    receive_message(HyParViewPeer from, GossipedPlumtreeMessage message) noexcept {
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

    // The count of a gossiped summary is a slot count, so it cannot claim
    // more ids than the summary holds.
    [[nodiscard]] constexpr std::expected<repair_plan_type, PlumtreeError>
    receive_ihave(HyParViewPeer from, GossipedPlumtreeIHave<MaxHistory> ihave) noexcept {
        auto peer_idx = find_link_(from.value().uuid);
        if (!peer_idx) {
            return std::unexpected(PlumtreeError::UnknownPeer);
        }

        repair_plan_type out{.source = from.value()};
        for (PlumtreeMessageHash const id : ihave.value().live()) {
            if (id == 0) {
                return std::unexpected(PlumtreeError::EmptyMessage);
            }
            if (!seen_(id)) {
                // The plan and the summary have the same MaxHistory slots.
                (void)out.requested.push(id);
            }
        }
        if (out.requested.count != 0) {
            promote_eager_(*peer_idx);
        }
        return out;
    }

    [[nodiscard]] constexpr ihave_type ihave_summary() const noexcept {
        ihave_type out{};
        for (std::uint16_t i = 0; i < history_count_; ++i) {
            const std::uint16_t idx =
                static_cast<std::uint16_t>((static_cast<std::size_t>(history_cursor_) + MaxHistory
                                            - static_cast<std::size_t>(history_count_) + static_cast<std::size_t>(i))
                                           % MaxHistory);
            // The summary and the history have the same MaxHistory slots.
            (void)out.push(history_[idx]);
        }
        return out;
    }

private:
    // Links are dense and a link is never removed, so the live links are
    // [0, link_count_).
    struct LinkSlot {
        cog::CogIdentity peer{};
        PlumtreeLinkState state = PlumtreeLinkState::Lazy;
    };

    // The shape gate of the mint gives each active peer a link slot, and
    // the overlay holds only distinct peers with a non-zero uuid, so no
    // link of the walk can refuse.
    template <std::size_t HyMaxActive, std::size_t HyMaxPassive>
    constexpr PlumtreeBroadcast(HyParViewMembership<HyMaxActive, HyMaxPassive> const& membership,
                                PlumtreeConfig config) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(admit_plumtree_config<MaxPeers>(config).has_value());
        for (cog::CogIdentity const& peer : membership.active_view().as_span()) {
            CRUCIBLE_FATAL_INVARIANT(add_link_(peer, PlumtreeLinkState::Eager).has_value());
        }
    }

    template <std::size_t P, std::size_t H, std::size_t A, std::size_t Q>
        requires PlumtreeFitsOverlay<P, H, A, Q>
    friend constexpr PlumtreeBroadcast<P, H> mint_plumtree(::foundation::effects::Init,
                                                           HyParViewMembership<A, Q> const&, PlumtreeConfig) noexcept;

    [[nodiscard]] std::expected<PlumtreeMessage, PlumtreeError>
    make_message_(std::span<const std::byte> payload) const noexcept {
        if (payload.empty()) {
            return std::unexpected(PlumtreeError::EmptyMessage);
        }
        if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
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

    [[nodiscard]] constexpr std::expected<void, PlumtreeError> add_link_(cog::CogIdentity peer,
                                                                         PlumtreeLinkState state) noexcept {
        if (peer.uuid.is_zero()) {
            return std::unexpected(PlumtreeError::ZeroUuid);
        }
        if (find_link_(peer.uuid).has_value()) {
            return std::unexpected(PlumtreeError::DuplicatePeer);
        }
        const auto slot = link_count_.reserve_next();
        if (!slot) {
            return std::unexpected(PlumtreeError::CapacityExceeded);
        }
        if (state == PlumtreeLinkState::Eager && eager_count_ == config_.max_eager_fanout.value()) {
            state = PlumtreeLinkState::Lazy;
        }
        links_.at(*slot) = LinkSlot{.peer = peer, .state = state};
        if (state == PlumtreeLinkState::Eager) {
            ++eager_count_;
        }
        return {};
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, PlumtreeError> find_link_(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < link_count_; ++i) {
            if (links_[i].peer.uuid == peer) {
                return i;
            }
        }
        return std::unexpected(PlumtreeError::PeerNotFound);
    }

    constexpr void promote_eager_(std::uint16_t idx) noexcept {
        if (links_[idx].state == PlumtreeLinkState::Eager) {
            return;
        }
        if (eager_count_ == config_.max_eager_fanout.value()) {
            for (std::uint16_t i = 0; i < link_count_; ++i) {
                if (i != idx && links_[i].state == PlumtreeLinkState::Eager) {
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

    [[nodiscard]] constexpr bool seen_(PlumtreeMessageHash id) const noexcept {
        for (std::uint16_t i = 0; i < history_count_; ++i) {
            if (history_[i] == id) {
                return true;
            }
        }
        return false;
    }

    constexpr void remember_(PlumtreeMessageHash id) noexcept {
        if (seen_(id)) {
            return;
        }
        history_[history_cursor_] = id;
        history_cursor_ =
            static_cast<std::uint16_t>((static_cast<std::size_t>(history_cursor_) + std::size_t{1}) % MaxHistory);
        if (history_count_ < MaxHistory) {
            ++history_count_;
        }
    }

    [[nodiscard]] constexpr broadcast_plan_type build_broadcast_plan_(PlumtreeMessage message,
                                                                      cog::Uuid except) const noexcept {
        broadcast_plan_type out{.message = message};
        for (std::uint16_t i = 0; i < link_count_; ++i) {
            LinkSlot const& link = links_[i];
            if (link.peer.uuid == except) {
                continue;
            }
            // Each plan table has MaxPeers slots, and at most link_count_
            // links reach them.
            if (link.state == PlumtreeLinkState::Eager) {
                (void)out.eager_peers.push(link.peer);
            } else {
                (void)out.lazy_peers.push(link.peer);
            }
        }
        return out;
    }

    PlumtreeConfig config_{};
    ::fixy::FixedArray<LinkSlot, MaxPeers> links_{};
    ::fixy::FixedArray<PlumtreeMessageHash, MaxHistory> history_{};
    SlotCount<MaxPeers> link_count_{};
    std::uint16_t eager_count_ = 0;
    std::uint16_t history_count_ = 0;
    std::uint16_t history_cursor_ = 0;
};

static_assert(!std::is_default_constructible_v<PlumtreeBroadcast<4, 8>>);
static_assert(!std::is_copy_constructible_v<PlumtreeBroadcast<4, 8>>);
static_assert(!std::is_move_constructible_v<PlumtreeBroadcast<4, 8>>);

template <std::size_t MaxPeers, std::size_t MaxHistory, std::size_t HyMaxActive, std::size_t HyMaxPassive>
    requires PlumtreeFitsOverlay<MaxPeers, MaxHistory, HyMaxActive, HyMaxPassive>
[[nodiscard]] constexpr PlumtreeBroadcast<MaxPeers, MaxHistory>
mint_plumtree(::foundation::effects::Init, HyParViewMembership<HyMaxActive, HyMaxPassive> const& membership,
              PlumtreeConfig config) noexcept {
    return PlumtreeBroadcast<MaxPeers, MaxHistory>{membership, config};
}

}  // namespace crucible::canopy
