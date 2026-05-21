#pragma once

// GAPS-172 substrate slice. CNT-P XDP_TX gossip multicast plans.
//
// This header does not emit verifier bytecode, clone packets in kernel, or
// attach programs to a live NIC. It pins the userspace contract first:
// source-tagged multicast plans, source-tagged topic IDs, bounded neighbor
// tables, XDP program/map descriptors, and deterministic in-process
// replication planning over dataplane::BpfMapImage.

#include <crucible/Platform.h>
#include <crucible/cntp/Integrity.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/cntp/dataplane/Xdp.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class GossipMulticastError : std::uint8_t {
    EmptyTopic,
    InvalidTopicHash,
    InvalidDedupWindow,
    InvalidPayloadLimit,
    InvalidPeer,
    DuplicateNeighbor,
    TooManyNeighbors,
    TooManyTopics,
    UnknownTopic,
    PacketTooLarge,
    EmptyPacket,
    IntegrityHashFailed,
};

[[nodiscard]] std::string_view
gossip_multicast_error_name(GossipMulticastError error) noexcept;

using GossipTopicHash = safety::Refined<safety::non_zero, std::uint64_t>;
using GossipDedupWindowNs = safety::Positive<std::uint64_t>;
using GossipPayloadBytes = safety::Positive<std::uint32_t>;

struct GossipTopicKey {
    std::uint64_t hash = 1;

    [[nodiscard]] friend constexpr bool
    operator==(GossipTopicKey, GossipTopicKey) noexcept = default;
};

using DeclaredGossipTopic =
    safety::Tagged<GossipTopicKey, safety::source::GossipMulticast>;

struct GossipNeighborTarget {
    cog::Uuid peer{};
    dataplane::XdpIfIndex ifindex{std::uint32_t{1}};
    std::array<std::byte, 6> mac{};
    std::uint32_t ipv4_be = 0;
};

template <std::uint16_t MaxNeighbors>
concept GossipNeighborShape = MaxNeighbors > 0;

template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
struct GossipNeighborList {
    std::array<GossipNeighborTarget, MaxNeighbors> entries{};
    std::uint16_t count = 0;

    [[nodiscard]] constexpr bool full() const noexcept {
        return count == MaxNeighbors;
    }

    [[nodiscard]] constexpr bool contains(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < count; ++i) {
            if (entries[static_cast<std::size_t>(i)].peer == peer) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::expected<void, GossipMulticastError>
    push(GossipNeighborTarget target) noexcept {
        if (target.peer.is_zero()) {
            return std::unexpected(GossipMulticastError::InvalidPeer);
        }
        if (contains(target.peer)) {
            return std::unexpected(GossipMulticastError::DuplicateNeighbor);
        }
        if (full()) {
            return std::unexpected(GossipMulticastError::TooManyNeighbors);
        }
        entries[static_cast<std::size_t>(count)] = target;
        ++count;
        return {};
    }
};

template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
concept GossipMulticastShape =
       MaxTopics > 0
    && GossipNeighborShape<MaxNeighbors>
    && sizeof(GossipTopicKey) <= std::numeric_limits<std::uint16_t>::max()
    && sizeof(GossipNeighborList<MaxNeighbors>) <=
           std::numeric_limits<std::uint16_t>::max();

struct GossipMulticastConfig {
    GossipDedupWindowNs dedup_window_ns{30'000'000'000ULL};
    GossipPayloadBytes max_payload_bytes{65'507U};
    bool use_hardware_replication = true;
};

struct GossipMulticastSpec {
    dataplane::DeclaredXdpProgram program{dataplane::XdpProgramSpec{}};
    dataplane::DeclaredBpfMap neighbor_map{dataplane::BpfMapSpec{}};
    GossipMulticastConfig config{};
};

using DeclaredGossipMulticastPlan =
    safety::Tagged<GossipMulticastSpec, safety::source::GossipMulticast>;

template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
struct GossipReplicationPlan {
    DeclaredGossipTopic topic{};
    IntegrityHash packet_id{std::uint64_t{1}};
    GossipNeighborList<MaxNeighbors> neighbors{};
    dataplane::XdpAction terminal_action = dataplane::XdpAction::Drop;
};

template <class Ctx>
concept CtxFitsGossipMulticastMint =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

[[nodiscard]] inline std::expected<DeclaredGossipTopic, GossipMulticastError>
admit_gossip_topic_hash(std::uint64_t hash) noexcept {
    if (hash == 0) {
        return std::unexpected(GossipMulticastError::InvalidTopicHash);
    }
    return DeclaredGossipTopic{GossipTopicKey{.hash = hash}};
}

[[nodiscard]] inline std::expected<DeclaredGossipTopic, GossipMulticastError>
admit_gossip_topic(std::string_view topic) noexcept {
    if (topic.empty()) {
        return std::unexpected(GossipMulticastError::EmptyTopic);
    }
    auto bytes = std::as_bytes(std::span{topic.data(), topic.size()});
    auto hash = xxhash64(bytes);
    if (!hash.has_value()) {
        return std::unexpected(GossipMulticastError::IntegrityHashFailed);
    }
    return DeclaredGossipTopic{GossipTopicKey{.hash = hash->value()}};
}

[[nodiscard]] inline std::expected<GossipDedupWindowNs, GossipMulticastError>
admit_gossip_dedup_window_ns(std::uint64_t ns) noexcept {
    if (ns == 0) {
        return std::unexpected(GossipMulticastError::InvalidDedupWindow);
    }
    return GossipDedupWindowNs{ns, typename GossipDedupWindowNs::Trusted{}};
}

[[nodiscard]] inline std::expected<GossipPayloadBytes, GossipMulticastError>
admit_gossip_payload_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(GossipMulticastError::InvalidPayloadLimit);
    }
    return GossipPayloadBytes{bytes, typename GossipPayloadBytes::Trusted{}};
}

[[nodiscard]] constexpr GossipNeighborTarget
gossip_neighbor_target(cog::CogIdentity const& peer,
                       dataplane::XdpIfIndex ifindex,
                       std::array<std::byte, 6> mac,
                       std::uint32_t ipv4_be) noexcept {
    return GossipNeighborTarget{
        .peer = peer.uuid,
        .ifindex = ifindex,
        .mac = mac,
        .ipv4_be = ipv4_be,
    };
}

[[nodiscard]] constexpr dataplane::DeclaredXdpProgram
gossip_multicast_xdp_program(DeclaredGossipMulticastPlan plan) noexcept {
    return plan.value().program;
}

template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
    requires GossipMulticastShape<MaxTopics, MaxNeighbors>
class GossipMulticastPlan
    : public safety::Pinned<GossipMulticastPlan<MaxTopics, MaxNeighbors>> {
public:
    using neighbor_list_type = GossipNeighborList<MaxNeighbors>;
    using neighbor_map_type =
        dataplane::BpfMapImage<GossipTopicKey, neighbor_list_type, MaxTopics,
                        dataplane::BpfMapKind::LruHash>;

    static constexpr std::uint32_t max_topics = MaxTopics;
    static constexpr std::uint16_t max_neighbors = MaxNeighbors;

private:
    DeclaredGossipMulticastPlan spec_{};
    neighbor_map_type neighbors_{};

public:
    explicit constexpr GossipMulticastPlan(
        DeclaredGossipMulticastPlan spec) noexcept
        : spec_{spec} {}

    [[nodiscard]] constexpr DeclaredGossipMulticastPlan
    spec() const noexcept {
        return spec_;
    }

    [[nodiscard]] constexpr std::uint32_t topic_count() const noexcept {
        return neighbors_.size();
    }

    [[nodiscard]] constexpr std::expected<void, GossipMulticastError>
    register_neighbor(DeclaredGossipTopic topic,
                      GossipNeighborTarget target) noexcept {
        auto key = topic.value();
        auto list = neighbors_.lookup(key).value_or(neighbor_list_type{});
        auto pushed = list.push(target);
        if (!pushed.has_value()) {
            return std::unexpected(pushed.error());
        }
        auto updated = neighbors_.update(key, list, dataplane::BpfMapUpdate::Any);
        if (!updated.has_value()) {
            return std::unexpected(GossipMulticastError::TooManyTopics);
        }
        return {};
    }

    [[nodiscard]] constexpr std::optional<neighbor_list_type>
    neighbors_for(DeclaredGossipTopic topic) const noexcept {
        return neighbors_.lookup(topic.value());
    }

    [[nodiscard]] std::expected<GossipReplicationPlan<MaxNeighbors>,
                                GossipMulticastError>
    plan_packet(DeclaredGossipTopic topic,
                std::span<const std::byte> packet) const noexcept {
        if (packet.empty()) {
            return std::unexpected(GossipMulticastError::EmptyPacket);
        }
        if (packet.size() > spec_.value().config.max_payload_bytes.value()) {
            return std::unexpected(GossipMulticastError::PacketTooLarge);
        }
        auto neighbors = neighbors_for(topic);
        if (!neighbors.has_value()) {
            return std::unexpected(GossipMulticastError::UnknownTopic);
        }
        auto packet_id = xxhash64(packet);
        if (!packet_id.has_value()) {
            return std::unexpected(GossipMulticastError::IntegrityHashFailed);
        }
        return GossipReplicationPlan<MaxNeighbors>{
            .topic = topic,
            .packet_id = *packet_id,
            .neighbors = *neighbors,
            .terminal_action = dataplane::XdpAction::Drop,
        };
    }
};

template <std::uint32_t MaxTopics,
          std::uint16_t MaxNeighbors,
          class Ctx>
    requires GossipMulticastShape<MaxTopics, MaxNeighbors>
          && CtxFitsGossipMulticastMint<Ctx>
[[nodiscard]] constexpr GossipMulticastPlan<MaxTopics, MaxNeighbors>
mint_gossip_multicast_plan(Ctx const& ctx,
                           NicInterfaceName iface,
                           dataplane::XdpIfIndex ifindex,
                           GossipMulticastConfig config = {},
                           dataplane::XdpMode mode = dataplane::XdpMode::Native) noexcept {
    auto xdp = dataplane::mint_xdp_program(ctx, iface, ifindex,
                                    dataplane::XdpProgramKind::GossipMulticast,
                                    mode);
    dataplane::BpfMapSpec map{
        .kind = dataplane::BpfMapKind::LruHash,
        .key_bytes = dataplane::PositiveMapElementBytes{
            static_cast<std::uint16_t>(sizeof(GossipTopicKey)),
            typename dataplane::PositiveMapElementBytes::Trusted{}},
        .value_bytes = dataplane::PositiveMapElementBytes{
            static_cast<std::uint16_t>(
                sizeof(GossipNeighborList<MaxNeighbors>)),
            typename dataplane::PositiveMapElementBytes::Trusted{}},
        .max_entries = dataplane::PositiveMapEntries{
            MaxTopics,
            typename dataplane::PositiveMapEntries::Trusted{}},
    };
    return GossipMulticastPlan<MaxTopics, MaxNeighbors>{
        DeclaredGossipMulticastPlan{GossipMulticastSpec{
            .program = xdp,
            .neighbor_map = dataplane::DeclaredBpfMap{map},
            .config = config,
        }}};
}

static_assert(sizeof(GossipTopicHash) == sizeof(std::uint64_t));
static_assert(sizeof(DeclaredGossipTopic) == sizeof(GossipTopicKey));
static_assert(sizeof(DeclaredGossipMulticastPlan) ==
              sizeof(GossipMulticastSpec));
static_assert(std::has_unique_object_representations_v<GossipTopicKey>);
static_assert(dataplane::BpfKey<GossipTopicKey>);
static_assert(dataplane::BpfScalar<GossipNeighborTarget>);

}  // namespace crucible::cntp
