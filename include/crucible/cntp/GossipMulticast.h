#pragma once

// Nothing here emits verifier bytecode, clones a packet in the kernel or
// attaches a program to a NIC.  The neighbor table is a process-local map
// image and plan_packet only describes the replication a dataplane would do.

#include <crucible/cntp/Integrity.h>
#include <crucible/cntp/dataplane/Xdp.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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

[[nodiscard]] constexpr std::string_view gossip_multicast_error_name(GossipMulticastError error) noexcept {
    return ::foundation::reflect::enum_name(error);
}

using GossipDedupWindowNs = ::fixy::Positive<std::uint64_t>;
using GossipPayloadBytes = ::fixy::Positive<std::uint32_t>;

struct GossipTopicKey {
    std::uint64_t hash = 1;

    [[nodiscard]] friend constexpr bool operator==(GossipTopicKey, GossipTopicKey) noexcept = default;
};

using DeclaredGossipTopic = ::fixy::Tagged<GossipTopicKey, ::fixy::tags::source::GossipMulticast>;

// A neighbor list is a map value, and a map value is a byte record: the
// kernel copies it and so does BpfMapImage.  A refined member would forbid
// that copy, so the interface index is stored as its word and the
// refinement lives at the two ends.  The constructor takes an XdpIfIndex and
// ifindex() hands one back; the stored word is at least one on every path,
// the default slot included, so the mint in ifindex() never fires.
class GossipNeighborTarget {
public:
    constexpr GossipNeighborTarget() noexcept = default;

    constexpr GossipNeighborTarget(cog::Uuid peer, dataplane::XdpIfIndex ifindex, std::array<std::byte, 6> mac,
                                   std::uint32_t ipv4_be) noexcept
        : peer_{peer}, ifindex_{ifindex.value()}, mac_{mac}, ipv4_be_{ipv4_be} {}

    [[nodiscard]] constexpr cog::Uuid peer() const noexcept { return peer_; }
    [[nodiscard]] constexpr dataplane::XdpIfIndex ifindex() const noexcept {
        return ::fixy::mint_refined<::fixy::positive>(ifindex_);
    }
    [[nodiscard]] constexpr std::array<std::byte, 6> mac() const noexcept { return mac_; }
    [[nodiscard]] constexpr std::uint32_t ipv4_be() const noexcept { return ipv4_be_; }

private:
    cog::Uuid peer_{};
    std::uint32_t ifindex_ = 1;
    std::array<std::byte, 6> mac_{};
    std::uint32_t ipv4_be_ = 0;
};

template <std::uint16_t MaxNeighbors>
concept GossipNeighborShape = MaxNeighbors > 0;

template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
struct GossipNeighborList {
    std::array<GossipNeighborTarget, MaxNeighbors> entries{};
    std::uint16_t count = 0;

    // count is public, so push() is not the only writer and the stored
    // value need not be one push() ever produced.  Both members below
    // therefore clamp against MaxNeighbors rather than trust it.  With
    // the equality test this used to carry, a count of 200 on an
    // 8-element list read as "not full" and push() then wrote
    // entries[200]; the loop below read the same range.
    [[nodiscard]] constexpr bool full() const noexcept { return count >= MaxNeighbors; }

    [[nodiscard]] constexpr bool contains(cog::Uuid peer) const noexcept {
        for (std::uint16_t i = 0; i < count && i < MaxNeighbors; ++i) {
            if (entries[static_cast<std::size_t>(i)].peer() == peer) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::expected<void, GossipMulticastError> push(GossipNeighborTarget target) noexcept {
        if (target.peer().is_zero()) {
            return std::unexpected(GossipMulticastError::InvalidPeer);
        }
        if (contains(target.peer())) {
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

// The plan stores each topic's neighbor list in a BPF map, so the shape is
// exactly what a map admits: the topic key and the list are map elements.
template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
concept GossipMulticastShape = MaxTopics > 0 && GossipNeighborShape<MaxNeighbors>
                            && dataplane::BpfMapElement<GossipTopicKey>
                            && dataplane::BpfMapElement<GossipNeighborList<MaxNeighbors>>;

struct GossipMulticastConfig {
    GossipDedupWindowNs dedup_window_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{30000000000ULL});
    GossipPayloadBytes max_payload_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{65507U});
    bool use_hardware_replication = true;
};

struct GossipMulticastSpec {
    dataplane::DeclaredXdpProgram program;
    dataplane::DeclaredBpfMap neighbor_map;
    GossipMulticastConfig config{};
};

using DeclaredGossipMulticastPlan = ::fixy::Tagged<GossipMulticastSpec, ::fixy::tags::source::GossipMulticast>;

template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
struct GossipReplicationPlan {
    DeclaredGossipTopic topic{};
    IntegrityHash packet_id = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{1});
    GossipNeighborList<MaxNeighbors> neighbors{};
    dataplane::XdpAction terminal_action = dataplane::XdpAction::Drop;
};

// The plan mints an XDP program, so it needs what that mint needs.
template <class Ctx>
concept CtxFitsGossipMulticastMint = dataplane::CtxFitsXdpMint<Ctx>;

[[nodiscard]] constexpr std::expected<DeclaredGossipTopic, GossipMulticastError>
admit_gossip_topic_hash(std::uint64_t hash) noexcept {
    if (hash == 0) {
        return std::unexpected(GossipMulticastError::InvalidTopicHash);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::GossipMulticast>(GossipTopicKey{.hash = hash});
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
    return ::fixy::mint_tagged<::fixy::tags::source::GossipMulticast>(GossipTopicKey{.hash = hash->value()});
}

[[nodiscard]] constexpr std::expected<GossipDedupWindowNs, GossipMulticastError>
admit_gossip_dedup_window_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, GossipMulticastError::InvalidDedupWindow);
}

[[nodiscard]] constexpr std::expected<GossipPayloadBytes, GossipMulticastError>
admit_gossip_payload_bytes(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, GossipMulticastError::InvalidPayloadLimit);
}

[[nodiscard]] constexpr GossipNeighborTarget gossip_neighbor_target(cog::CogIdentity const& peer,
                                                                    dataplane::XdpIfIndex ifindex,
                                                                    std::array<std::byte, 6> mac,
                                                                    std::uint32_t ipv4_be) noexcept {
    return GossipNeighborTarget{peer.uuid, ifindex, mac, ipv4_be};
}

[[nodiscard]] constexpr dataplane::DeclaredXdpProgram
gossip_multicast_xdp_program(DeclaredGossipMulticastPlan plan) noexcept {
    return plan.value().program;
}

template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
    requires GossipMulticastShape<MaxTopics, MaxNeighbors>
class GossipMulticastPlan : public ::foundation::Pinned<GossipMulticastPlan<MaxTopics, MaxNeighbors>> {
public:
    using neighbor_list_type = GossipNeighborList<MaxNeighbors>;
    using neighbor_map_type =
        dataplane::BpfMapImage<GossipTopicKey, neighbor_list_type, MaxTopics, dataplane::BpfMapKind::LruHash>;

    static constexpr std::uint32_t max_topics = MaxTopics;
    static constexpr std::uint16_t max_neighbors = MaxNeighbors;

private:
    DeclaredGossipMulticastPlan spec_{};
    neighbor_map_type neighbors_{};

public:
    explicit constexpr GossipMulticastPlan(DeclaredGossipMulticastPlan spec) noexcept : spec_{spec} {}

    [[nodiscard]] constexpr DeclaredGossipMulticastPlan spec() const noexcept { return spec_; }

    [[nodiscard]] constexpr std::uint32_t topic_count() const noexcept { return neighbors_.size(); }

    [[nodiscard]] constexpr std::expected<void, GossipMulticastError>
    register_neighbor(DeclaredGossipTopic topic, GossipNeighborTarget target) noexcept {
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

    [[nodiscard]] constexpr std::optional<neighbor_list_type> neighbors_for(DeclaredGossipTopic topic) const noexcept {
        return neighbors_.lookup(topic.value());
    }

    [[nodiscard]] std::expected<GossipReplicationPlan<MaxNeighbors>, GossipMulticastError>
    plan_packet(DeclaredGossipTopic topic, std::span<const std::byte> packet) const noexcept {
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

template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors, class Ctx>
    requires GossipMulticastShape<MaxTopics, MaxNeighbors> && CtxFitsGossipMulticastMint<Ctx>
[[nodiscard]] constexpr GossipMulticastPlan<MaxTopics, MaxNeighbors>
mint_gossip_multicast_plan(Ctx const& ctx, NicInterfaceName iface, dataplane::XdpIfIndex ifindex,
                           GossipMulticastConfig config = {},
                           dataplane::XdpMode mode = dataplane::XdpMode::Native) noexcept {
    return GossipMulticastPlan<MaxTopics, MaxNeighbors>{
        ::fixy::mint_tagged<::fixy::tags::source::GossipMulticast>(GossipMulticastSpec{
            .program =
                dataplane::mint_xdp_program(ctx, iface, ifindex, dataplane::XdpProgramKind::GossipMulticast, mode),
            .neighbor_map = dataplane::mint_bpf_map_spec<GossipTopicKey, GossipNeighborList<MaxNeighbors>>(
                dataplane::BpfMapKind::LruHash, ::fixy::mint_refined<::fixy::positive>(MaxTopics)),
            .config = config,
        })};
}

static_assert(sizeof(DeclaredGossipTopic) == sizeof(GossipTopicKey));
static_assert(sizeof(DeclaredGossipMulticastPlan) == sizeof(GossipMulticastSpec));
static_assert(std::has_unique_object_representations_v<GossipTopicKey>);
static_assert(dataplane::BpfKey<GossipTopicKey>);
static_assert(dataplane::BpfScalar<GossipNeighborTarget>);

}  // namespace crucible::cntp
