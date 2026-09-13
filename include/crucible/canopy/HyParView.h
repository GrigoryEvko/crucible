#pragma once

#include <crucible/Philox.h>
#include <crucible/Platform.h>
#include <crucible/canopy/Swim.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::canopy {

template <std::size_t MaxActive, std::size_t MaxPassive>
concept HyParViewShape = MaxActive > 0 && MaxPassive > 0
                      && MaxActive <= MaxPassive&& MaxPassive
                             <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
using HyParViewCount = safety::Refined<safety::bounded_above<static_cast<std::uint16_t>(Capacity)>, std::uint16_t>;

using HyParViewDurationNs = safety::Refined<safety::positive, std::uint64_t>;
using HyParViewPositiveCount = safety::Refined<safety::positive, std::uint16_t>;
using HyParViewPeer = safety::Tagged<cog::CogIdentity, safety::source::HyParView>;

enum class HyParViewError : std::uint8_t {
    ActiveViewFull,
    DuplicatePeer,
    EmptyActiveView,
    InvalidConfig,
    PeerNotFound,
    ZeroUuid,
};

[[nodiscard]] std::string_view hyparview_error_name(HyParViewError error) noexcept;

struct HyParViewConfig {
    HyParViewPositiveCount active_size{5};
    HyParViewPositiveCount passive_size{30};
    HyParViewPositiveCount active_random_walk_length{6};
    HyParViewPositiveCount passive_random_walk_length{6};
    HyParViewPositiveCount active_random_walk_acceptance{3};
    HyParViewDurationNs shuffle_period_ns{30000000000ULL};
};

template <std::size_t MaxPassive>
    requires(MaxPassive > 0)
struct HyParViewShuffle {
    safety::FixedArray<cog::CogIdentity, MaxPassive> peers{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr HyParViewCount<MaxPassive> size() const noexcept {
        return HyParViewCount<MaxPassive>{count, typename HyParViewCount<MaxPassive>::Trusted{}};
    }
};

template <std::size_t MaxPassive>
    requires(MaxPassive > 0)
using GossipedHyParViewShuffle = safety::Tagged<HyParViewShuffle<MaxPassive>, safety::source::Gossiped>;

template <std::size_t MaxPassive>
    requires(MaxPassive > 0)
struct HyParViewShufflePlan {
    cog::CogIdentity target{};
    HyParViewShuffle<MaxPassive> sample{};
};

template <std::size_t MaxActive>
    requires(MaxActive > 0)
struct HyParViewForwardJoinPlan {
    safety::FixedArray<cog::CogIdentity, MaxActive> targets{};
    std::uint16_t count = 0;
    cog::CogIdentity joining{};
    std::uint16_t ttl = 0;

    [[nodiscard]] constexpr HyParViewCount<MaxActive> size() const noexcept {
        return HyParViewCount<MaxActive>{count, typename HyParViewCount<MaxActive>::Trusted{}};
    }
};

template <std::size_t MaxActive = 8, std::size_t MaxPassive = 64>
    requires HyParViewShape<MaxActive, MaxPassive>
class HyParViewMembership : public safety::Pinned<HyParViewMembership<MaxActive, MaxPassive>> {
public:
    using active_view_type = safety::Borrowed<const cog::CogIdentity, safety::source::HyParView>;
    using passive_view_type = safety::Borrowed<const cog::CogIdentity, safety::source::HyParView>;
    using shuffle_type = HyParViewShuffle<MaxPassive>;
    using shuffle_plan_type = HyParViewShufflePlan<MaxPassive>;
    using forward_join_plan_type = HyParViewForwardJoinPlan<MaxActive>;

    explicit HyParViewMembership(HyParViewConfig config = {}) noexcept : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(config_fits_shape_());
    }

    HyParViewMembership(HyParViewConfig config, std::span<const HyParViewPeer> active_peers,
                        std::span<const HyParViewPeer> passive_peers = {}) noexcept
        : config_{config} {
        CRUCIBLE_FATAL_INVARIANT(config_fits_shape_());
        for (HyParViewPeer peer : active_peers) {
            CRUCIBLE_FATAL_INVARIANT(join(peer).has_value());
        }
        for (HyParViewPeer peer : passive_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_passive(peer).has_value());
        }
    }

    [[nodiscard]] HyParViewConfig config() const noexcept { return config_; }

    [[nodiscard]] HyParViewCount<MaxActive> active_size() const noexcept {
        return HyParViewCount<MaxActive>{active_count_, typename HyParViewCount<MaxActive>::Trusted{}};
    }

    [[nodiscard]] HyParViewCount<MaxPassive> passive_size() const noexcept {
        return HyParViewCount<MaxPassive>{passive_count_, typename HyParViewCount<MaxPassive>::Trusted{}};
    }

    [[nodiscard]] active_view_type active_view() const noexcept {
        return active_view_type{active_.data(), active_count_};
    }

    [[nodiscard]] passive_view_type passive_view() const noexcept {
        return passive_view_type{passive_.data(), passive_count_};
    }

    [[nodiscard]] std::expected<void, HyParViewError> join(HyParViewPeer peer) noexcept { return add_active_(peer); }

    [[nodiscard]] std::expected<void, HyParViewError> add_passive(HyParViewPeer peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (contains_active_(id.uuid) || contains_passive_(id.uuid)) {
            return std::unexpected(HyParViewError::DuplicatePeer);
        }
        add_passive_unique_(id);
        return {};
    }

    [[nodiscard]] std::expected<void, HyParViewError> on_swim_event(GossipedSwimEvent event) noexcept {
        SwimEvent const& raw = event.value();
        if (raw.peer.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (raw.state == SwimState::Dead) {
            return mark_failed(raw.peer.uuid);
        }
        if (!contains_active_(raw.peer.uuid) && !contains_passive_(raw.peer.uuid)) {
            add_passive_unique_(raw.peer);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, HyParViewError> mark_failed(cog::Uuid peer_id) noexcept {
        const bool removed_active = remove_active_(peer_id);
        const bool removed_passive = remove_passive_(peer_id);
        if (!removed_active && !removed_passive) {
            return std::unexpected(HyParViewError::PeerNotFound);
        }
        if (removed_active) {
            promote_passive_();
        }
        return {};
    }

    [[nodiscard]] std::expected<shuffle_plan_type, HyParViewError> shuffle_plan() noexcept {
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
                out.sample.peers[out.sample.count] = passive_[idx];
                ++out.sample.count;
            }
        }
        return out;
    }

    [[nodiscard]] std::expected<void, HyParViewError>
    apply_shuffle(GossipedHyParViewShuffle<MaxPassive> shuffle) noexcept {
        shuffle_type const& raw = shuffle.value();
        if (raw.count > MaxPassive) {
            return std::unexpected(HyParViewError::InvalidConfig);
        }
        for (std::uint16_t i = 0; i < raw.count; ++i) {
            cog::CogIdentity peer = raw.peers[i];
            if (peer.uuid.is_zero()) {
                return std::unexpected(HyParViewError::ZeroUuid);
            }
            if (!contains_active_(peer.uuid) && !contains_passive_(peer.uuid)) {
                add_passive_unique_(peer);
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<forward_join_plan_type, HyParViewError>
    forward_join_plan(HyParViewPeer joining) const noexcept {
        if (joining.value().uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }

        forward_join_plan_type out{.joining = joining.value(),
                                   .ttl = static_cast<std::uint16_t>(config_.active_random_walk_length.value())};
        const std::uint16_t target_limit =
            std::min<std::uint16_t>(active_count_, config_.active_random_walk_acceptance.value());
        for (std::uint16_t i = 0; i < active_count_; ++i) {
            if (active_[i].uuid == joining.value().uuid) {
                continue;
            }
            out.targets[out.count] = active_[i];
            ++out.count;
            if (out.count == target_limit) {
                break;
            }
        }
        return out;
    }

private:
    [[nodiscard]] bool config_fits_shape_() const noexcept {
        return config_.active_size.value() <= MaxActive && config_.passive_size.value() <= MaxPassive
            && config_.active_size.value() <= config_.passive_size.value();
    }

    [[nodiscard]] bool contains_active_(cog::Uuid uuid) const noexcept {
        for (std::uint16_t i = 0; i < active_count_; ++i) {
            if (active_[i].uuid == uuid) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool contains_passive_(cog::Uuid uuid) const noexcept {
        for (std::uint16_t i = 0; i < passive_count_; ++i) {
            if (passive_[i].uuid == uuid) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::expected<void, HyParViewError> add_active_(HyParViewPeer peer) noexcept {
        cog::CogIdentity const& id = peer.value();
        if (id.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (contains_active_(id.uuid) || contains_passive_(id.uuid)) {
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

    void add_passive_unique_(cog::CogIdentity peer) noexcept {
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

    [[nodiscard]] bool remove_active_(cog::Uuid uuid) noexcept {
        for (std::uint16_t i = 0; i < active_count_; ++i) {
            if (active_[i].uuid != uuid) {
                continue;
            }
            const std::uint16_t last = static_cast<std::uint16_t>(active_count_ - std::uint16_t{1});
            active_[i] = active_[last];
            active_[last] = cog::CogIdentity{};
            --active_count_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool remove_passive_(cog::Uuid uuid) noexcept {
        for (std::uint16_t i = 0; i < passive_count_; ++i) {
            if (passive_[i].uuid != uuid) {
                continue;
            }
            const std::uint16_t last = static_cast<std::uint16_t>(passive_count_ - std::uint16_t{1});
            passive_[i] = passive_[last];
            passive_[last] = cog::CogIdentity{};
            --passive_count_;
            return true;
        }
        return false;
    }

    void promote_passive_() noexcept {
        if (passive_count_ == 0 || active_count_ == config_.active_size.value()) {
            return;
        }
        // The random pick carries the same partition-healing argument as the
        // shuffle plan.
        const Philox::Ctr rand = next_random_();
        const std::uint16_t idx = static_cast<std::uint16_t>(rand[0] % passive_count_);
        cog::CogIdentity promoted = passive_[idx];
        (void)remove_passive_(promoted.uuid);
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

    Philox::Ctr next_random_() noexcept {
        const Philox::Ctr out = Philox::generate(rng_counter_, rng_seed_);
        ++rng_counter_;
        return out;
    }

    HyParViewConfig config_{};
    safety::FixedArray<cog::CogIdentity, MaxActive> active_{};
    safety::FixedArray<cog::CogIdentity, MaxPassive> passive_{};
    std::uint16_t active_count_ = 0;
    std::uint16_t passive_count_ = 0;
    std::uint64_t rng_seed_ = 0;
    std::uint64_t rng_counter_ = 0;
};

static_assert(!std::is_copy_constructible_v<HyParViewMembership<4, 8>>);
static_assert(!std::is_move_constructible_v<HyParViewMembership<4, 8>>);

[[nodiscard]] inline std::expected<HyParViewPeer, HyParViewError> admit_hyparview_peer(cog::CogIdentity peer) noexcept {
    if (peer.uuid.is_zero()) {
        return std::unexpected(HyParViewError::ZeroUuid);
    }
    return HyParViewPeer{peer};
}

template <std::size_t MaxActive = 8, std::size_t MaxPassive = 64, class Ctx>
    requires HyParViewShape<MaxActive, MaxPassive> && std::same_as<Ctx, effects::Init>
[[nodiscard]] HyParViewMembership<MaxActive, MaxPassive>
mint_hyparview(Ctx, std::span<const HyParViewPeer> active_peers = {}, std::span<const HyParViewPeer> passive_peers = {},
               HyParViewConfig config = {}) noexcept {
    return HyParViewMembership<MaxActive, MaxPassive>{config, active_peers, passive_peers};
}

// The mint above and the peer-list constructor abort on input that breaks a
// capacity or peer-shape precondition.  The two helpers below run the same
// checks and return the outcome, so a caller holding unchecked input recovers
// instead of dying.  The mint stays the convenience for input that is already
// known to be valid.
template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] constexpr std::expected<HyParViewConfig, HyParViewError>
admit_hyparview_config(HyParViewConfig config) noexcept {
    if (config.active_size.value() > MaxActive) {
        return std::unexpected(HyParViewError::InvalidConfig);
    }
    if (config.passive_size.value() > MaxPassive) {
        return std::unexpected(HyParViewError::InvalidConfig);
    }
    if (config.active_size.value() > config.passive_size.value()) {
        return std::unexpected(HyParViewError::InvalidConfig);
    }
    return config;
}

template <std::size_t MaxActive, std::size_t MaxPassive>
    requires HyParViewShape<MaxActive, MaxPassive>
[[nodiscard]] std::expected<void, HyParViewError>
populate_hyparview_membership(HyParViewMembership<MaxActive, MaxPassive>& membership,
                              std::span<const HyParViewPeer> active_peers,
                              std::span<const HyParViewPeer> passive_peers = {}) noexcept {
    // Check both capacities before any insertion, so that a failure part way
    // through cannot leave the membership half populated.
    if (active_peers.size() > membership.config().active_size.value()) {
        return std::unexpected(HyParViewError::ActiveViewFull);
    }
    if (passive_peers.size() > membership.config().passive_size.value()) {
        return std::unexpected(HyParViewError::ActiveViewFull);
    }
    for (HyParViewPeer peer : active_peers) {
        auto rc = membership.join(peer);
        if (!rc.has_value()) {
            return std::unexpected(rc.error());
        }
    }
    for (HyParViewPeer peer : passive_peers) {
        auto rc = membership.add_passive(peer);
        if (!rc.has_value()) {
            return std::unexpected(rc.error());
        }
    }
    return {};
}

}  // namespace crucible::canopy
