#pragma once

// GAPS-139 substrate slice. CNT-P application-layer overlay multicast plans.
//
// This is not the live Splitstream/Plumtree transport. HyParView and Plumtree
// are still separate pending owners. This header pins the type-level substrate
// they will consume: source-tagged overlay peers, bounded stripe/recovery/fanout
// config, deterministic per-stripe parent/child route construction, and
// bounded message stripe planning.

#include <crucible/Platform.h>   // CRUCIBLE_FATAL_INVARIANT
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
        // FIXY-U-080 / fixy-A5-014 + A5-035: was __builtin_trap (silent SIGILL).
        CRUCIBLE_FATAL_INVARIANT(
            config_.stripe_count.value() <= MaxStripes &&
            config_.fanout.value() <= MaxFanout &&
            config_.recovery_threshold.value() <=
                config_.stripe_count.value());
        local_ = local_peer.value();
        CRUCIBLE_FATAL_INVARIANT(add_peer(local_peer).has_value());
        for (DeclaredOverlayPeer const& peer : initial_peers) {
            CRUCIBLE_FATAL_INVARIANT(add_peer(peer).has_value());
        }
        CRUCIBLE_FATAL_INVARIANT(rebuild_routes().has_value());
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

    [[nodiscard]] constexpr bool
    route_less_(std::uint16_t lhs,
                std::uint16_t rhs,
                std::uint8_t stripe) const noexcept {
        const auto lhs_hash = overlay_detail::stripe_hash(peers_[lhs], stripe);
        const auto rhs_hash = overlay_detail::stripe_hash(peers_[rhs], stripe);
        if (lhs_hash != rhs_hash) {
            return lhs_hash < rhs_hash;
        }
        if (peers_[lhs].uuid.hi != peers_[rhs].uuid.hi) {
            return peers_[lhs].uuid.hi < peers_[rhs].uuid.hi;
        }
        return peers_[lhs].uuid.lo < peers_[rhs].uuid.lo;
    }

    [[nodiscard]] std::expected<void, OverlayMulticastError>
    rebuild_routes() noexcept {
        if (peer_count_ == 0) {
            return {};
        }

        for (std::uint8_t stripe = 0; stripe < config_.stripe_count.value();
             ++stripe) {
            safety::FixedArray<std::uint16_t, MaxPeers + 1u> order{};
            for (std::uint16_t i = 0; i < peer_count_; ++i) {
                std::uint16_t pos = i;
                while (pos > 0 &&
                       route_less_(i, order[pos - 1u], stripe)) {
                    order[pos] = order[pos - 1u];
                    --pos;
                }
                order[pos] = i;
            }

            std::size_t local_pos = 0;
            for (std::size_t pos = 0; pos < peer_count_; ++pos) {
                if (peers_[order[pos]] == local_) {
                    local_pos = pos;
                    break;
                }
            }

            route_type route{.stripe = stripe};
            if (local_pos > 0) {
                const auto fanout =
                    static_cast<std::size_t>(config_.fanout.value());
                const std::size_t parent_pos = (local_pos - 1u) / fanout;
                route.has_parent = true;
                route.parent = peers_[order[parent_pos]];
            }

            const auto fanout =
                static_cast<std::size_t>(config_.fanout.value());
            const std::size_t first_child = local_pos * fanout + 1u;
            const std::size_t end_child = first_child + fanout;
            for (std::size_t pos = first_child;
                 pos < peer_count_ && pos < end_child;
                 ++pos) {
                route.children[route.child_count] = peers_[order[pos]];
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
