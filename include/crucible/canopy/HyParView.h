#pragma once

// Bounded HyParView membership substrate for Canopy.
//
// This layer owns active/passive view bookkeeping, deterministic repair from
// passive to active on SWIM failure events, and bounded shuffle planning.
// TCP keepalive sockets and downstream event channels remain transport owners;
// this substrate exposes typed values those layers can consume.

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
concept HyParViewShape =
    MaxActive > 0 &&
    MaxPassive > 0 &&
    MaxActive <= MaxPassive &&
    MaxPassive <= static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max());

template <std::size_t Capacity>
    requires (Capacity > 0 &&
              Capacity <= static_cast<std::size_t>(
                  std::numeric_limits<std::uint16_t>::max()))
using HyParViewCount =
    safety::Refined<safety::bounded_above<
                        static_cast<std::uint16_t>(Capacity)>,
                    std::uint16_t>;

using HyParViewDurationNs =
    safety::Refined<safety::positive, std::uint64_t>;
using HyParViewPositiveCount =
    safety::Refined<safety::positive, std::uint16_t>;
using HyParViewPeer =
    safety::Tagged<cog::CogIdentity, safety::source::HyParView>;

enum class HyParViewError : std::uint8_t {
    ActiveViewFull,
    DuplicatePeer,
    EmptyActiveView,
    InvalidConfig,
    PeerNotFound,
    ZeroUuid,
};

[[nodiscard]] std::string_view
hyparview_error_name(HyParViewError error) noexcept;

struct HyParViewConfig {
    HyParViewPositiveCount active_size{5};
    HyParViewPositiveCount passive_size{30};
    HyParViewPositiveCount active_random_walk_length{6};
    HyParViewPositiveCount passive_random_walk_length{6};
    HyParViewPositiveCount active_random_walk_acceptance{3};
    HyParViewDurationNs shuffle_period_ns{30'000'000'000ULL};
};

template <std::size_t MaxPassive>
    requires (MaxPassive > 0)
struct HyParViewShuffle {
    safety::FixedArray<cog::CogIdentity, MaxPassive> peers{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr HyParViewCount<MaxPassive>
    size() const noexcept {
        return HyParViewCount<MaxPassive>{
            count,
            typename HyParViewCount<MaxPassive>::Trusted{}};
    }
};

template <std::size_t MaxPassive>
    requires (MaxPassive > 0)
using GossipedHyParViewShuffle =
    safety::Tagged<HyParViewShuffle<MaxPassive>, safety::source::Gossiped>;

template <std::size_t MaxPassive>
    requires (MaxPassive > 0)
struct HyParViewShufflePlan {
    cog::CogIdentity target{};
    HyParViewShuffle<MaxPassive> sample{};
};

template <std::size_t MaxActive>
    requires (MaxActive > 0)
struct HyParViewForwardJoinPlan {
    safety::FixedArray<cog::CogIdentity, MaxActive> targets{};
    std::uint16_t count = 0;
    cog::CogIdentity joining{};
    std::uint16_t ttl = 0;

    [[nodiscard]] constexpr HyParViewCount<MaxActive>
    size() const noexcept {
        return HyParViewCount<MaxActive>{
            count,
            typename HyParViewCount<MaxActive>::Trusted{}};
    }
};

template <std::size_t MaxActive = 8, std::size_t MaxPassive = 64>
    requires HyParViewShape<MaxActive, MaxPassive>
class HyParViewMembership
    : public safety::Pinned<HyParViewMembership<MaxActive, MaxPassive>> {
public:
    using active_view_type =
        safety::Borrowed<const cog::CogIdentity, safety::source::HyParView>;
    using passive_view_type =
        safety::Borrowed<const cog::CogIdentity, safety::source::HyParView>;
    using shuffle_type = HyParViewShuffle<MaxPassive>;
    using shuffle_plan_type = HyParViewShufflePlan<MaxPassive>;
    using forward_join_plan_type = HyParViewForwardJoinPlan<MaxActive>;

    explicit HyParViewMembership(HyParViewConfig config = {}) noexcept
        : config_{config} {
        if (!config_fits_shape_()) [[unlikely]] {
            __builtin_trap();
        }
    }

    HyParViewMembership(
        HyParViewConfig config,
        std::span<const HyParViewPeer> active_peers,
        std::span<const HyParViewPeer> passive_peers = {}) noexcept
        : config_{config} {
        if (!config_fits_shape_()) [[unlikely]] {
            __builtin_trap();
        }
        for (HyParViewPeer peer : active_peers) {
            if (!join(peer).has_value()) [[unlikely]] {
                __builtin_trap();
            }
        }
        for (HyParViewPeer peer : passive_peers) {
            if (!add_passive(peer).has_value()) [[unlikely]] {
                __builtin_trap();
            }
        }
    }

    [[nodiscard]] HyParViewConfig config() const noexcept {
        return config_;
    }

    [[nodiscard]] HyParViewCount<MaxActive>
    active_size() const noexcept {
        return HyParViewCount<MaxActive>{
            active_count_,
            typename HyParViewCount<MaxActive>::Trusted{}};
    }

    [[nodiscard]] HyParViewCount<MaxPassive>
    passive_size() const noexcept {
        return HyParViewCount<MaxPassive>{
            passive_count_,
            typename HyParViewCount<MaxPassive>::Trusted{}};
    }

    [[nodiscard]] active_view_type active_view() const noexcept {
        return active_view_type{active_.data(), active_count_};
    }

    [[nodiscard]] passive_view_type passive_view() const noexcept {
        return passive_view_type{passive_.data(), passive_count_};
    }

    [[nodiscard]] std::expected<void, HyParViewError>
    join(HyParViewPeer peer) noexcept {
        return add_active_(peer);
    }

    [[nodiscard]] std::expected<void, HyParViewError>
    add_passive(HyParViewPeer peer) noexcept {
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

    [[nodiscard]] std::expected<void, HyParViewError>
    on_swim_event(GossipedSwimEvent event) noexcept {
        SwimEvent const& raw = event.value();
        if (raw.peer.uuid.is_zero()) {
            return std::unexpected(HyParViewError::ZeroUuid);
        }
        if (raw.state == SwimState::Dead) {
            return mark_failed(raw.peer.uuid);
        }
        if (!contains_active_(raw.peer.uuid) &&
            !contains_passive_(raw.peer.uuid)) {
            add_passive_unique_(raw.peer);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, HyParViewError>
    mark_failed(cog::Uuid peer_id) noexcept {
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

    [[nodiscard]] std::expected<shuffle_plan_type, HyParViewError>
    shuffle_plan() noexcept {
        if (active_count_ == 0) {
            return std::unexpected(HyParViewError::EmptyActiveView);
        }

        // FIXY-U-107 / fixy-A5-013: replace round-robin cursor with
        // Philox-derived target selection.  Deterministic round-robin
        // pinned partition-healing — under any persistent network
        // partition the same peer subset got shuffled-into forever
        // (the rotation order never breaks the partition).  Philox
        // gives statistically uniform selection, breaking the pin.
        // Seed mixes UUIDs of every joined peer (per-node unique,
        // replay-safe via deterministic per-call counter).
        const Philox::Ctr rand = next_random_();
        const std::uint16_t target_idx =
            static_cast<std::uint16_t>(rand[0] % active_count_);

        shuffle_plan_type out{.target = active_[target_idx]};
        const std::uint16_t limit = std::min<std::uint16_t>(
            config_.passive_random_walk_length.value(),
            passive_count_);
        if (limit != 0) {
            const std::uint16_t passive_start = static_cast<std::uint16_t>(
                rand[1] % passive_count_);
            for (std::uint16_t i = 0; i < limit; ++i) {
                const std::uint16_t idx = static_cast<std::uint16_t>(
                    (passive_start + i) % passive_count_);
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

        forward_join_plan_type out{
            .joining = joining.value(),
            .ttl = static_cast<std::uint16_t>(
                config_.active_random_walk_length.value())};
        const std::uint16_t target_limit = std::min<std::uint16_t>(
            active_count_, config_.active_random_walk_acceptance.value());
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
        return config_.active_size.value() <= MaxActive &&
               config_.passive_size.value() <= MaxPassive &&
               config_.active_size.value() <= config_.passive_size.value();
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

    [[nodiscard]] std::expected<void, HyParViewError>
    add_active_(HyParViewPeer peer) noexcept {
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
        // FIXY-U-107: Philox-derived eviction slot (was passive_cursor_).
        const Philox::Ctr rand = next_random_();
        const std::uint16_t idx =
            static_cast<std::uint16_t>(rand[0] % passive_count_);
        passive_[idx] = peer;
    }

    [[nodiscard]] bool remove_active_(cog::Uuid uuid) noexcept {
        for (std::uint16_t i = 0; i < active_count_; ++i) {
            if (active_[i].uuid != uuid) {
                continue;
            }
            const std::uint16_t last =
                static_cast<std::uint16_t>(active_count_ - std::uint16_t{1});
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
            const std::uint16_t last =
                static_cast<std::uint16_t>(passive_count_ - std::uint16_t{1});
            passive_[i] = passive_[last];
            passive_[last] = cog::CogIdentity{};
            --passive_count_;
            return true;
        }
        return false;
    }

    void promote_passive_() noexcept {
        if (passive_count_ == 0 ||
            active_count_ == config_.active_size.value()) {
            return;
        }
        // FIXY-U-107: Philox-derived promotion pick (was passive_cursor_).
        // Same partition-resistance rationale as shuffle_plan_.
        const Philox::Ctr rand = next_random_();
        const std::uint16_t idx =
            static_cast<std::uint16_t>(rand[0] % passive_count_);
        cog::CogIdentity promoted = passive_[idx];
        (void)remove_passive_(promoted.uuid);
        active_[active_count_] = promoted;
        ++active_count_;
    }

    // FIXY-U-107 / fixy-A5-013: per-instance Philox RNG state replaces
    // the old passive_cursor_ / shuffle_cursor_ round-robin pair.
    // Seed mixes the UUIDs of every peer that joins active or passive
    // (via add_active_ + add_passive_unique_), so two HyParView
    // instances seeded with different membership history pick
    // different shuffle sequences.  Counter increments monotonically
    // per RNG call; replay is bit-stable for a given event sequence.
    //
    // mix_uuid_seed_ uses FNV-1a-style avalanche; cryptographic
    // quality is not required (the goal is partition-healing, not
    // adversarial unpredictability — see fixy-A5-013 commentary).
    [[nodiscard]] static constexpr std::uint64_t
    mix_uuid_seed_(std::uint64_t seed, cog::Uuid u) noexcept {
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

[[nodiscard]] inline std::expected<HyParViewPeer, HyParViewError>
admit_hyparview_peer(cog::CogIdentity peer) noexcept {
    if (peer.uuid.is_zero()) {
        return std::unexpected(HyParViewError::ZeroUuid);
    }
    return HyParViewPeer{peer};
}

template <std::size_t MaxActive = 8,
          std::size_t MaxPassive = 64,
          class Ctx>
    requires HyParViewShape<MaxActive, MaxPassive> &&
             std::same_as<Ctx, effects::Init>
[[nodiscard]] HyParViewMembership<MaxActive, MaxPassive>
mint_hyparview(
    Ctx,
    std::span<const HyParViewPeer> active_peers = {},
    std::span<const HyParViewPeer> passive_peers = {},
    HyParViewConfig config = {}) noexcept {
    return HyParViewMembership<MaxActive, MaxPassive>{
        config, active_peers, passive_peers};
}

}  // namespace crucible::canopy
