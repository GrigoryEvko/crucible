#pragma once

// GAPS-139 substrate slice. CNT-P application-layer overlay multicast plans.
//
// This is not the live Splitstream/Plumtree transport. HyParView and Plumtree
// are still separate pending owners. This header pins the type-level substrate
// they will consume: source-tagged overlay peers, bounded stripe/recovery/fanout
// config, deterministic per-stripe parent/child route construction, and
// bounded message stripe planning.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class OverlayMulticastError : std::uint8_t {
    InvalidPeer,
    DuplicatePeer,
    TooManyPeers,
    InvalidStripeCount,
    InvalidRecoveryThreshold,
    InvalidFanout,
    UnknownStripe,
    FanoutExceeded,
    EmptyMessage,
    MessageTooLarge,
};

[[nodiscard]] std::string_view
overlay_multicast_error_name(OverlayMulticastError error) noexcept;

using OverlayStripeCount =
    safety::Bounded<std::uint8_t{1}, std::uint8_t{64}, std::uint8_t>;
using OverlayRecoveryThreshold =
    safety::Bounded<std::uint8_t{1}, std::uint8_t{64}, std::uint8_t>;
using OverlayFanout =
    safety::Bounded<std::uint8_t{1}, std::uint8_t{16}, std::uint8_t>;
using OverlayPayloadBytes = safety::Positive<std::uint32_t>;

struct OverlayPeerRef {
    cog::Uuid uuid{};

    [[nodiscard]] friend constexpr bool
    operator==(OverlayPeerRef, OverlayPeerRef) noexcept = default;
};

using DeclaredOverlayPeer =
    safety::Tagged<OverlayPeerRef, safety::source::OverlayMulticast>;

struct OverlayMulticastConfig {
    OverlayStripeCount stripe_count{
        std::uint8_t{8}, typename OverlayStripeCount::Trusted{}};
    OverlayRecoveryThreshold recovery_threshold{
        std::uint8_t{5}, typename OverlayRecoveryThreshold::Trusted{}};
    OverlayFanout fanout{
        std::uint8_t{2}, typename OverlayFanout::Trusted{}};
    OverlayPayloadBytes max_payload_bytes{65'507U};
    bool use_fec_per_stripe = true;
};

template <std::size_t MaxPeers,
          std::size_t MaxStripes,
          std::size_t MaxFanout>
concept OverlayMulticastShape =
    MaxPeers > 0 &&
    MaxStripes > 0 &&
    MaxStripes <= 64 &&
    MaxFanout > 0 &&
    MaxFanout <= 16;

template <class Ctx>
concept CtxFitsOverlayMulticastMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

[[nodiscard]] constexpr std::expected<OverlayStripeCount,
                                      OverlayMulticastError>
admit_overlay_stripe_count(std::uint8_t stripes) noexcept {
    if (stripes == 0u || stripes > 64u) {
        return std::unexpected(OverlayMulticastError::InvalidStripeCount);
    }
    return OverlayStripeCount{stripes,
                              typename OverlayStripeCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<OverlayRecoveryThreshold,
                                      OverlayMulticastError>
admit_overlay_recovery_threshold(std::uint8_t threshold,
                                 OverlayStripeCount stripes) noexcept {
    if (threshold == 0u || threshold > stripes.value()) {
        return std::unexpected(
            OverlayMulticastError::InvalidRecoveryThreshold);
    }
    return OverlayRecoveryThreshold{
        threshold, typename OverlayRecoveryThreshold::Trusted{}};
}

[[nodiscard]] constexpr std::expected<OverlayFanout, OverlayMulticastError>
admit_overlay_fanout(std::uint8_t fanout) noexcept {
    if (fanout == 0u || fanout > 16u) {
        return std::unexpected(OverlayMulticastError::InvalidFanout);
    }
    return OverlayFanout{fanout, typename OverlayFanout::Trusted{}};
}

[[nodiscard]] constexpr std::expected<OverlayPayloadBytes,
                                      OverlayMulticastError>
admit_overlay_payload_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(OverlayMulticastError::MessageTooLarge);
    }
    return OverlayPayloadBytes{
        bytes, typename OverlayPayloadBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DeclaredOverlayPeer,
                                      OverlayMulticastError>
admit_overlay_peer(cog::CogIdentity peer) noexcept {
    if (peer.uuid.is_zero()) {
        return std::unexpected(OverlayMulticastError::InvalidPeer);
    }
    return DeclaredOverlayPeer{OverlayPeerRef{.uuid = peer.uuid}};
}

template <std::size_t MaxFanout>
    requires (MaxFanout > 0)
struct OverlayStripeRoute {
    std::uint8_t stripe = 0;
    bool has_parent = false;
    OverlayPeerRef parent{};
    safety::FixedArray<OverlayPeerRef, MaxFanout> children{};
    std::uint8_t child_count = 0;
};

struct OverlayStripeSlice {
    std::uint8_t stripe = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

template <std::size_t MaxStripes>
    requires (MaxStripes > 0)
struct OverlayMessagePlan {
    safety::FixedArray<OverlayStripeSlice, MaxStripes> stripes{};
    std::uint8_t stripe_count = 0;
};

namespace overlay_detail {

[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30U;
    x *= 0xbf58'476d'1ce4'e5b9ULL;
    x ^= x >> 27U;
    x *= 0x94d0'49bb'1331'11ebULL;
    x ^= x >> 31U;
    return x;
}

[[nodiscard]] constexpr std::uint64_t
stripe_hash(OverlayPeerRef peer, std::uint8_t stripe) noexcept {
    return mix64(peer.uuid.hi ^ mix64(peer.uuid.lo) ^
                 (std::uint64_t{stripe} * 0x9e37'79b9'7f4a'7c15ULL));
}

}  // namespace overlay_detail

template <std::size_t MaxPeers,
          std::size_t MaxStripes,
          std::size_t MaxFanout>
    requires OverlayMulticastShape<MaxPeers, MaxStripes, MaxFanout>
class OverlayMulticastPlan
    : public safety::Pinned<
          OverlayMulticastPlan<MaxPeers, MaxStripes, MaxFanout>> {
public:
    using route_type = OverlayStripeRoute<MaxFanout>;
    using message_plan_type = OverlayMessagePlan<MaxStripes>;

    explicit OverlayMulticastPlan(
        DeclaredOverlayPeer local_peer,
        std::span<const DeclaredOverlayPeer> initial_peers = {},
        OverlayMulticastConfig config = {}) noexcept
        : config_{config} {
        if (config_.stripe_count.value() > MaxStripes ||
            config_.fanout.value() > MaxFanout ||
            config_.recovery_threshold.value() > config_.stripe_count.value()) {
            __builtin_trap();
        }
        if (!add_peer(local_peer).has_value()) [[unlikely]] {
            __builtin_trap();
        }
        local_ = local_peer.value();
        for (DeclaredOverlayPeer const& peer : initial_peers) {
            if (!add_peer(peer).has_value()) [[unlikely]] {
                __builtin_trap();
            }
        }
        if (!rebuild_routes().has_value()) [[unlikely]] {
            __builtin_trap();
        }
    }

    [[nodiscard]] std::expected<void, OverlayMulticastError>
    add_peer(DeclaredOverlayPeer peer) noexcept {
        OverlayPeerRef raw = peer.value();
        if (raw.uuid.is_zero()) {
            return std::unexpected(OverlayMulticastError::InvalidPeer);
        }
        if (contains_peer_(raw)) {
            return std::unexpected(OverlayMulticastError::DuplicatePeer);
        }
        if (peer_count_ == MaxPeers + 1u) {
            return std::unexpected(OverlayMulticastError::TooManyPeers);
        }
        peers_[peer_count_] = raw;
        ++peer_count_;
        return rebuild_routes();
    }

    [[nodiscard]] constexpr OverlayMulticastConfig config() const noexcept {
        return config_;
    }

    [[nodiscard]] constexpr std::uint16_t peer_count() const noexcept {
        return peer_count_;
    }

    [[nodiscard]] constexpr OverlayPeerRef local_peer() const noexcept {
        return local_;
    }

    [[nodiscard]] std::expected<route_type, OverlayMulticastError>
    route_for(std::uint8_t stripe) const noexcept {
        if (stripe >= config_.stripe_count.value()) {
            return std::unexpected(OverlayMulticastError::UnknownStripe);
        }
        return routes_[stripe];
    }

    [[nodiscard]] std::expected<message_plan_type, OverlayMulticastError>
    plan_message(std::span<const std::byte> message) const noexcept {
        if (message.empty()) {
            return std::unexpected(OverlayMulticastError::EmptyMessage);
        }
        if (message.size() > config_.max_payload_bytes.value()) {
            return std::unexpected(OverlayMulticastError::MessageTooLarge);
        }

        message_plan_type out{};
        const auto stripes = config_.stripe_count.value();
        const auto base = message.size() / stripes;
        const auto rem = message.size() % stripes;
        std::size_t offset = 0;
        for (std::uint8_t stripe = 0; stripe < stripes; ++stripe) {
            const auto extra = stripe < rem ? std::size_t{1} : std::size_t{0};
            const auto len = base + extra;
            out.stripes[stripe] = OverlayStripeSlice{
                .stripe = stripe,
                .offset = static_cast<std::uint32_t>(offset),
                .size = static_cast<std::uint32_t>(len),
            };
            offset += len;
        }
        out.stripe_count = stripes;
        return out;
    }

private:
    OverlayMulticastConfig config_{};
    OverlayPeerRef local_{};
    safety::FixedArray<OverlayPeerRef, MaxPeers + 1u> peers_{};
    std::uint16_t peer_count_ = 0;
    safety::FixedArray<route_type, MaxStripes> routes_{};

    [[nodiscard]] constexpr bool
    contains_peer_(OverlayPeerRef peer) const noexcept {
        for (std::uint16_t i = 0; i < peer_count_; ++i) {
            if (peers_[i] == peer) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t
    parent_index_for_(std::uint16_t child_idx, std::uint8_t stripe) const
        noexcept {
        const auto child_hash =
            overlay_detail::stripe_hash(peers_[child_idx], stripe);
        std::uint16_t best = child_idx;
        std::uint64_t best_hash = 0;
        bool found_lower = false;

        for (std::uint16_t i = 0; i < peer_count_; ++i) {
            if (i == child_idx) {
                continue;
            }
            const auto h = overlay_detail::stripe_hash(peers_[i], stripe);
            if (h < child_hash && (!found_lower || h > best_hash)) {
                best = i;
                best_hash = h;
                found_lower = true;
            }
        }
        if (found_lower) {
            return best;
        }

        for (std::uint16_t i = 0; i < peer_count_; ++i) {
            if (i == child_idx) {
                continue;
            }
            const auto h = overlay_detail::stripe_hash(peers_[i], stripe);
            if (best == child_idx || h > best_hash) {
                best = i;
                best_hash = h;
            }
        }
        return best;
    }

    [[nodiscard]] std::expected<void, OverlayMulticastError>
    rebuild_routes() noexcept {
        if (peer_count_ == 0) {
            return {};
        }

        for (std::uint8_t stripe = 0; stripe < config_.stripe_count.value();
             ++stripe) {
            route_type route{.stripe = stripe};
            if (peer_count_ > 1) {
                const std::uint16_t parent = parent_index_for_(0, stripe);
                if (parent != 0) {
                    route.has_parent = true;
                    route.parent = peers_[parent];
                }
            }

            for (std::uint16_t i = 1; i < peer_count_; ++i) {
                if (parent_index_for_(i, stripe) != 0) {
                    continue;
                }
                if (route.child_count >= config_.fanout.value()) {
                    return std::unexpected(
                        OverlayMulticastError::FanoutExceeded);
                }
                route.children[route.child_count] = peers_[i];
                ++route.child_count;
            }
            routes_[stripe] = route;
        }
        return {};
    }
};

template <std::size_t MaxPeers,
          std::size_t MaxStripes,
          std::size_t MaxFanout,
          class Ctx>
    requires OverlayMulticastShape<MaxPeers, MaxStripes, MaxFanout>
          && CtxFitsOverlayMulticastMint<Ctx>
[[nodiscard]] OverlayMulticastPlan<MaxPeers, MaxStripes, MaxFanout>
mint_overlay_multicast(Ctx const&,
                       DeclaredOverlayPeer local_peer,
                       std::span<const DeclaredOverlayPeer> initial_peers = {},
                       OverlayMulticastConfig config = {}) noexcept {
    return OverlayMulticastPlan<MaxPeers, MaxStripes, MaxFanout>{
        local_peer, initial_peers, config};
}

static_assert(sizeof(OverlayStripeCount) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayRecoveryThreshold) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayFanout) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayPayloadBytes) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredOverlayPeer) == sizeof(OverlayPeerRef));
static_assert(std::is_trivially_copyable_v<OverlayPeerRef>);
static_assert(std::is_trivially_copyable_v<OverlayMulticastConfig>);
static_assert(OverlayMulticastShape<8, 8, 2>);
static_assert(!OverlayMulticastShape<0, 8, 2>);
static_assert(!OverlayMulticastShape<8, 0, 2>);
static_assert(!OverlayMulticastShape<8, 8, 0>);

}  // namespace crucible::cntp
