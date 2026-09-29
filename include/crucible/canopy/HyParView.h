#pragma once

#include <crucible/Philox.h>
#include <crucible/Platform.h>
#include <crucible/canopy/SlotTable.h>
#include <crucible/canopy/Swim.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/Borrowed.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>

namespace crucible::canopy {

// The two views are slot tables, and the passive view is at least as large
// as the active view.
template <std::size_t MaxActive, std::size_t MaxPassive>
concept HyParViewShape = SlotCapacity<MaxActive> && SlotCapacity<MaxPassive> && MaxActive <= MaxPassive;

using HyParViewDurationNs = ::fixy::Refined<::fixy::positive, std::uint64_t>;
using HyParViewPositiveCount = ::fixy::Refined<::fixy::positive, std::uint16_t>;
using HyParViewPeer = ::fixy::Tagged<cog::CogIdentity, ::fixy::tags::source::HyParView>;

// foundation::reflect::enum_name gives the log spelling.
enum class HyParViewError : std::uint8_t {
    ActiveViewFull,
    DuplicatePeer,
    EmptyActiveView,
    InvalidConfig,
    PassiveViewFull,
    PeerNotFound,
    ZeroUuid,
};

struct HyParViewConfig {
    HyParViewPositiveCount active_size = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{5});
    HyParViewPositiveCount passive_size = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{30});
    HyParViewPositiveCount active_random_walk_length = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{6});
    HyParViewPositiveCount passive_random_walk_length = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{6});
    HyParViewPositiveCount active_random_walk_acceptance = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{3});
    HyParViewDurationNs shuffle_period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{30000000000});
};

// A sample of a passive view, which a shuffle carries to a peer.
template <std::size_t MaxPassive>
    requires SlotCapacity<MaxPassive>
struct HyParViewShuffle : SlotTable<cog::CogIdentity, MaxPassive> {};

template <std::size_t MaxPassive>
    requires SlotCapacity<MaxPassive>
using GossipedHyParViewShuffle = ::fixy::Tagged<HyParViewShuffle<MaxPassive>, ::fixy::tags::source::Gossiped>;

template <std::size_t MaxPassive>
    requires SlotCapacity<MaxPassive>
struct HyParViewShufflePlan {
    cog::CogIdentity target{};
    HyParViewShuffle<MaxPassive> sample{};
};

template <std::size_t MaxActive>
    requires SlotCapacity<MaxActive>
struct HyParViewForwardJoinPlan {
    SlotTable<cog::CogIdentity, MaxActive> targets{};
    cog::CogIdentity joining{};
    std::uint16_t ttl = 0;
};

// The recoverable check of a configuration: the sizes fit the capacities,
// and the active size is not larger than the passive size.  The mint does
// the same check and stops the process on a refusal.
template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr std::expected<HyParViewConfig, HyParViewError>
admit_hyparview_config(HyParViewConfig config) noexcept {
    if (config.active_size.value() > MaxActive || config.passive_size.value() > MaxPassive
        || config.active_size.value() > config.passive_size.value()) {
        return std::unexpected(HyParViewError::InvalidConfig);
    }
    return config;
}

template <std::size_t MaxActive = 8, std::size_t MaxPassive = 64>
    requires HyParViewShape<MaxActive, MaxPassive>
class HyParViewMembership;

// The recoverable walk of a peer list.  It checks each peer and the room of
// each view before the first insertion, so a refusal leaves the membership
// as it was.  O((a + p) * (a + p + MaxActive + MaxPassive)) for a active
// and p passive peers.  The mint does the same walk and stops the process
// on a refusal.
template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr std::expected<void, HyParViewError>
populate_hyparview_membership(HyParViewMembership<MaxActive, MaxPassive>& membership,
                              std::span<const HyParViewPeer> active_peers,
                              std::span<const HyParViewPeer> passive_peers = {}) noexcept;

// The one door: a membership holds the overlay state of a process, so only
// a context that owns Init builds one.
template <std::size_t MaxActive = 8, std::size_t MaxPassive = 64>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr HyParViewMembership<MaxActive, MaxPassive>
mint_hyparview(::foundation::effects::Init, std::span<const HyParViewPeer> active_peers = {},
               std::span<const HyParViewPeer> passive_peers = {}, HyParViewConfig config = {}) noexcept;

template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
class HyParViewMembership : public ::foundation::Pinned<HyParViewMembership<MaxActive, MaxPassive>> {
public:
    using active_view_type = ::fixy::Borrowed<const cog::CogIdentity, ::fixy::tags::source::HyParView>;
    using passive_view_type = ::fixy::Borrowed<const cog::CogIdentity, ::fixy::tags::source::HyParView>;
    using shuffle_type = HyParViewShuffle<MaxPassive>;
    using shuffle_plan_type = HyParViewShufflePlan<MaxPassive>;
    using forward_join_plan_type = HyParViewForwardJoinPlan<MaxActive>;

    [[nodiscard]] constexpr HyParViewConfig config() const noexcept { return config_; }

    // Each insertion checks the configured size, and the configured size
    // fits the capacity.
    [[nodiscard]] constexpr BoundedSlotCount<MaxActive> active_size() const noexcept {
        return ::fixy::mint_refined_trusted<slot_count_bound<MaxActive>>(active_count_);
    }

    [[nodiscard]] constexpr BoundedSlotCount<MaxPassive> passive_size() const noexcept {
        return ::fixy::mint_refined_trusted<slot_count_bound<MaxPassive>>(passive_count_);
    }

    [[nodiscard]] constexpr active_view_type active_view() const noexcept {
        return active_view_type{active_.data(), active_count_};
    }

    [[nodiscard]] constexpr passive_view_type passive_view() const noexcept {
        return passive_view_type{passive_.data(), passive_count_};
    }

    [[nodiscard]] constexpr std::expected<void, HyParViewError> join(HyParViewPeer peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (contains_(id.uuid)) {
            return std::unexpected(HyParViewError::DuplicatePeer);
        }
        if (active_count_ == config_.active_size.value()) {
            return std::unexpected(HyParViewError::ActiveViewFull);
        }
        rng_seed_ = mix_uuid_seed_(rng_seed_, id.uuid);
        active_[active_count_] = id;
        ++active_count_;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, HyParViewError> add_passive(HyParViewPeer peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (contains_(id.uuid)) {
            return std::unexpected(HyParViewError::DuplicatePeer);
        }
        add_passive_unique_(id);
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, HyParViewError> on_swim_event(GossipedSwimEvent event) noexcept {
        SwimEvent const& raw = event.value();
        if (raw.peer.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (raw.state == SwimState::Dead) {
            return mark_failed(raw.peer.uuid);
        }
        if (!contains_(raw.peer.uuid)) {
            add_passive_unique_(raw.peer);
        }
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, HyParViewError> mark_failed(cog::Uuid peer_id) noexcept {
        const bool removed_active = remove_(active_, active_count_, peer_id);
        const bool removed_passive = remove_(passive_, passive_count_, peer_id);
        if (!removed_active && !removed_passive) {
            return std::unexpected(HyParViewError::PeerNotFound);
        }
        if (removed_active) {
            promote_passive_();
        }
        return {};
    }

    [[nodiscard]] constexpr std::expected<shuffle_plan_type, HyParViewError> shuffle_plan() noexcept {
        if (active_count_ == 0) {
            return std::unexpected(HyParViewError::EmptyActiveView);
        }

        // A round-robin cursor pins partition healing.  Under a persistent
        // partition the rotation order keeps reaching the same subset of
        // peers, so nothing ever crosses the partition.  A pseudo-random
        // target is uniform over the active view and breaks that pin.
        const Philox::Ctr rand = next_random_();
        const std::uint16_t target_idx = static_cast<std::uint16_t>(rand[0] % active_count_);

        shuffle_plan_type out{.target = active_[target_idx]};
        const std::uint16_t limit = std::min<std::uint16_t>(config_.passive_random_walk_length.value(), passive_count_);
        if (limit != 0) {
            const std::uint16_t passive_start = static_cast<std::uint16_t>(rand[1] % passive_count_);
            for (std::uint16_t i = 0; i < limit; ++i) {
                const std::uint16_t idx = static_cast<std::uint16_t>((passive_start + i) % passive_count_);
                // The sample has MaxPassive slots, and limit is at most
                // passive_count_.
                (void)out.sample.push(passive_[idx]);
            }
        }
        return out;
    }

    // The count of a gossiped shuffle is a slot count, so it cannot claim
    // more peers than the sample holds.
    [[nodiscard]] constexpr std::expected<void, HyParViewError>
    apply_shuffle(GossipedHyParViewShuffle<MaxPassive> shuffle) noexcept {
        for (cog::CogIdentity const& peer : shuffle.value().live()) {
            if (peer.uuid.is_zero()) {
                return std::unexpected(HyParViewError::ZeroUuid);
            }
            if (!contains_(peer.uuid)) {
                add_passive_unique_(peer);
            }
        }
        return {};
    }

    [[nodiscard]] constexpr std::expected<forward_join_plan_type, HyParViewError>
    forward_join_plan(HyParViewPeer joining) const noexcept {
        if (joining.value().uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }

        forward_join_plan_type out{.joining = joining.value(), .ttl = config_.active_random_walk_length.value()};
        const std::uint16_t target_limit =
            std::min<std::uint16_t>(active_count_, config_.active_random_walk_acceptance.value());
        for (std::uint16_t i = 0; i < active_count_ && out.targets.count < target_limit; ++i) {
            if (active_[i].uuid != joining.value().uuid) {
                // The plan has MaxActive slots, and at most active_count_
                // peers reach it.
                (void)out.targets.push(active_[i]);
            }
        }
        return out;
    }

private:
    constexpr HyParViewMembership(std::span<const HyParViewPeer> active_peers,
                                  std::span<const HyParViewPeer> passive_peers, HyParViewConfig config) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT((admit_hyparview_config<MaxActive, MaxPassive>(config).has_value()));
        CRUCIBLE_FATAL_INVARIANT(populate_hyparview_membership(*this, active_peers, passive_peers).has_value());
    }

    template <std::size_t A, std::size_t P>
        requires HyParViewShape<A, P>
    friend constexpr HyParViewMembership<A, P> mint_hyparview(::foundation::effects::Init,
                                                              std::span<const HyParViewPeer>,
                                                              std::span<const HyParViewPeer>, HyParViewConfig) noexcept;

    // The two views are disjoint, so a peer is known when either holds it.
    [[nodiscard]] constexpr bool contains_(cog::Uuid uuid) const noexcept {
        auto const holds = [uuid](cog::CogIdentity const& peer) noexcept { return peer.uuid == uuid; };
        return std::ranges::any_of(active_view().as_span(), holds)
            || std::ranges::any_of(passive_view().as_span(), holds);
    }

    constexpr void add_passive_unique_(cog::CogIdentity peer) noexcept {
        rng_seed_ = mix_uuid_seed_(rng_seed_, peer.uuid);
        if (passive_count_ < config_.passive_size.value()) {
            passive_[passive_count_] = peer;
            ++passive_count_;
            return;
        }
        if (passive_count_ == 0) {
            return;
        }
        const Philox::Ctr rand = next_random_();
        const std::uint16_t idx = static_cast<std::uint16_t>(rand[0] % passive_count_);
        passive_[idx] = peer;
    }

    // Moves the last live peer of a view into the slot of the removed one.
    template <std::size_t Capacity>
    [[nodiscard]] static constexpr bool remove_(::fixy::FixedArray<cog::CogIdentity, Capacity>& view,
                                                std::uint16_t& count, cog::Uuid uuid) noexcept {
        for (std::uint16_t i = 0; i < count; ++i) {
            if (view[i].uuid != uuid) {
                continue;
            }
            const std::uint16_t last = static_cast<std::uint16_t>(count - std::uint16_t{1});
            view[i] = view[last];
            view[last] = cog::CogIdentity{};
            --count;
            return true;
        }
        return false;
    }

    constexpr void promote_passive_() noexcept {
        if (passive_count_ == 0 || active_count_ == config_.active_size.value()) {
            return;
        }
        // The random pick carries the same partition-healing argument as the
        // shuffle plan.
        const Philox::Ctr rand = next_random_();
        const std::uint16_t idx = static_cast<std::uint16_t>(rand[0] % passive_count_);
        cog::CogIdentity promoted = passive_[idx];
        (void)remove_(passive_, passive_count_, promoted.uuid);
        active_[active_count_] = promoted;
        ++active_count_;
    }

    // The seed mixes the uuid of every peer that joins either view, so two
    // instances with different membership history draw different sequences.
    // The counter advances once per draw, so replaying one event sequence
    // reproduces the same draws.  The goal is partition healing rather than
    // unpredictability against an adversary, so this mix needs no
    // cryptographic strength.
    [[nodiscard]] static constexpr std::uint64_t mix_uuid_seed_(std::uint64_t seed, cog::Uuid u) noexcept {
        seed ^= u.lo;
        seed = seed * 0x100000001b3ULL;
        seed ^= u.hi;
        seed = seed * 0x100000001b3ULL;
        return seed;
    }

    constexpr Philox::Ctr next_random_() noexcept {
        const Philox::Ctr out = Philox::generate(rng_counter_, rng_seed_);
        ++rng_counter_;
        return out;
    }

    HyParViewConfig config_{};
    ::fixy::FixedArray<cog::CogIdentity, MaxActive> active_{};
    ::fixy::FixedArray<cog::CogIdentity, MaxPassive> passive_{};
    std::uint16_t active_count_ = 0;
    std::uint16_t passive_count_ = 0;
    std::uint64_t rng_seed_ = 0;
    std::uint64_t rng_counter_ = 0;
};

static_assert(!std::is_default_constructible_v<HyParViewMembership<4, 8>>);
static_assert(!std::is_copy_constructible_v<HyParViewMembership<4, 8>>);
static_assert(!std::is_move_constructible_v<HyParViewMembership<4, 8>>);

// The admission door of a raw identity.  A zero uuid names no peer.
[[nodiscard]] constexpr std::expected<HyParViewPeer, HyParViewError>
admit_hyparview_peer(cog::CogIdentity peer) noexcept {
    if (peer.uuid.is_zero()) {
        return std::unexpected(HyParViewError::ZeroUuid);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::HyParView>(peer);
}

template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr HyParViewMembership<MaxActive, MaxPassive>
mint_hyparview(::foundation::effects::Init, std::span<const HyParViewPeer> active_peers,
               std::span<const HyParViewPeer> passive_peers, HyParViewConfig config) noexcept {
    return HyParViewMembership<MaxActive, MaxPassive>{active_peers, passive_peers, config};
}

template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr std::expected<void, HyParViewError>
populate_hyparview_membership(HyParViewMembership<MaxActive, MaxPassive>& membership,
                              std::span<const HyParViewPeer> active_peers,
                              std::span<const HyParViewPeer> passive_peers) noexcept {
    const HyParViewConfig config = membership.config();
    if (active_peers.size() > static_cast<std::size_t>(config.active_size.value() - membership.active_size().value())) {
        return std::unexpected(HyParViewError::ActiveViewFull);
    }
    if (passive_peers.size()
        > static_cast<std::size_t>(config.passive_size.value() - membership.passive_size().value())) {
        return std::unexpected(HyParViewError::PassiveViewFull);
    }

    // A peer is refused when it is zero, when a view holds it, or when an
    // entry before it names it.  `earlier` is the part of its own list
    // before it, and a passive peer is also checked against the active list.
    auto const check_peer =
        [&membership](cog::Uuid uuid,
                      std::span<const HyParViewPeer> earlier) noexcept -> std::expected<void, HyParViewError> {
        if (uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        auto const holds = [uuid](cog::CogIdentity const& peer) noexcept { return peer.uuid == uuid; };
        auto const names = [uuid](HyParViewPeer const& peer) noexcept { return peer.value().uuid == uuid; };
        const bool listed_before = std::ranges::any_of(earlier, names);
        const bool known = std::ranges::any_of(membership.active_view().as_span(), holds)
                        || std::ranges::any_of(membership.passive_view().as_span(), holds);
        if (listed_before || known) {
            return std::unexpected(HyParViewError::DuplicatePeer);
        }
        return {};
    };
    for (std::size_t i = 0; i < active_peers.size(); ++i) {
        if (auto checked = check_peer(active_peers[i].value().uuid, active_peers.first(i)); !checked) {
            return checked;
        }
    }
    for (std::size_t i = 0; i < passive_peers.size(); ++i) {
        const cog::Uuid uuid = passive_peers[i].value().uuid;
        auto const names = [uuid](HyParViewPeer const& peer) noexcept { return peer.value().uuid == uuid; };
        if (std::ranges::any_of(active_peers, names)) {
            return std::unexpected(HyParViewError::DuplicatePeer);
        }
        if (auto checked = check_peer(uuid, passive_peers.first(i)); !checked) {
            return checked;
        }
    }

    // Every check passed, so no insertion below can refuse.
    for (HyParViewPeer const& peer : active_peers) {
        CRUCIBLE_FATAL_INVARIANT(membership.join(peer).has_value());
    }
    for (HyParViewPeer const& peer : passive_peers) {
        CRUCIBLE_FATAL_INVARIANT(membership.add_passive(peer).has_value());
    }
    return {};
}

}  // namespace crucible::canopy
