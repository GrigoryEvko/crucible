#pragma once

// The XDP multicast of the gossip of the mesh, as a plan.  Nothing here emits
// verifier bytecode, clones a packet in the kernel or attaches a program to a
// NIC.  The neighbor table is a map image of this process, and plan_packet
// only tells which replication a dataplane does.  The XDP program of the
// plan is the stub canopy/bpf/gossip_xdp_tx.bpf.c.

#include <crucible/cntp/Integrity.h>
#include <crucible/cntp/dataplane/Xdp.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/SrIov.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>

#include <cstddef>
#include <cstdint>

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

[[nodiscard]] constexpr ::fixy::TextView gossip_multicast_error_name(GossipMulticastError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

using GossipDedupWindowNs = ::fixy::Positive<std::uint64_t>;
using GossipPayloadBytes = ::fixy::Positive<std::uint32_t>;

// An IPv4 address in network byte order.
using Ipv4AddressBe = ::fixy::Id<struct Ipv4AddressBeTag, std::uint32_t>;

struct GossipTopicKey {
    std::uint64_t hash = 1;

    [[nodiscard]] friend constexpr bool operator==(GossipTopicKey const&, GossipTopicKey const&) noexcept = default;
};

using DeclaredGossipTopic = ::fixy::Tagged<GossipTopicKey, ::fixy::tags::source::GossipMulticast>;

// A neighbor list is a map value, and a map value is a byte record: the
// kernel copies it, and BpfMapImage copies it too.  A refined member would
// refuse that copy, so the target stores the word of the interface index,
// and the refinement stands at the two ends.  The constructor takes an
// XdpIfIndex and ifindex() gives one back.  The stored word is at least one
// on each path, also in the default target, so the mint in ifindex() always
// holds.
class GossipNeighborTarget {
public:
    constexpr GossipNeighborTarget() noexcept = default;

    constexpr GossipNeighborTarget(cog::Uuid peer, dataplane::XdpIfIndex ifindex, cog::sriov::MacAddress mac,
                                   Ipv4AddressBe ipv4) noexcept
        : peer_{peer}, ifindex_{ifindex.value()}, mac_{mac}, ipv4_be_{ipv4.raw()} {}

    [[nodiscard]] constexpr cog::Uuid peer() const noexcept { return peer_; }
    [[nodiscard]] constexpr dataplane::XdpIfIndex ifindex() const noexcept {
        return ::fixy::mint_refined<::fixy::positive>(ifindex_);
    }
    [[nodiscard]] constexpr cog::sriov::MacAddress mac() const noexcept { return mac_; }
    [[nodiscard]] constexpr Ipv4AddressBe ipv4() const noexcept { return Ipv4AddressBe{ipv4_be_}; }

private:
    cog::Uuid peer_{};
    std::uint32_t ifindex_ = 1;
    cog::sriov::MacAddress mac_{};
    std::uint16_t pad_ = 0;
    std::uint32_t ipv4_be_ = 0;
};

template <std::uint16_t MaxNeighbors>
concept GossipNeighborShape = MaxNeighbors > 0;

// At most MaxNeighbors targets of distinct peers, in the order of their
// pushes.  push() is the one writer of the count, so the count never passes
// MaxNeighbors.  The count is a word of 64 bits, so the list has no padding
// byte.
template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
class GossipNeighborList {
    ::fixy::FixedArray<GossipNeighborTarget, MaxNeighbors> entries_{};
    std::uint64_t count_ = 0;

public:
    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }

    [[nodiscard]] constexpr bool is_full() const noexcept { return count_ >= MaxNeighbors; }

    // The targets in the order of their pushes.
    [[nodiscard]] constexpr ::fixy::View<GossipNeighborTarget const> targets() const& noexcept {
        return entries_.view().window(0, count_).expect("the count of a list is at most its capacity");
    }
    void targets() const&& = delete("a view of a temporary list points into an object that the full expression ends");

    // Complexity: one pass over the targets.
    [[nodiscard]] constexpr bool contains(cog::Uuid peer) const noexcept {
        for (GossipNeighborTarget const& target : targets()) {
            if (target.peer() == peer) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, GossipMulticastError>
    push(GossipNeighborTarget const& target) noexcept {
        if (target.peer().is_zero()) {
            return ::fixy::err(GossipMulticastError::InvalidPeer);
        }
        if (contains(target.peer())) {
            return ::fixy::err(GossipMulticastError::DuplicateNeighbor);
        }
        if (is_full()) {
            return ::fixy::err(GossipMulticastError::TooManyNeighbors);
        }
        entries_.set(::fixy::Index<MaxNeighbors>::check(count_).expect("a list that is not full has room"), target);
        ++count_;
        return ::fixy::Unit{};
    }
};

// The plan stores the neighbor list of each topic in a BPF map, so the shape
// is what a map admits: the topic key and the list are map elements.
template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
concept GossipMulticastShape = MaxTopics > 0
                            && GossipNeighborShape<MaxNeighbors>&& dataplane::BpfMapElement<GossipTopicKey>&&
                                   dataplane::BpfMapElement<GossipNeighborList<MaxNeighbors>>;

// The NIC replicates a packet with XDP_TX, or the kernel stack sends each
// copy.
enum class GossipReplication : std::uint8_t {
    Hardware = 0,
    Software = 1,
};

struct GossipMulticastConfig {
    GossipDedupWindowNs dedup_window_ns =
        ::fixy::mint_refined_constant<::fixy::positive, std::uint64_t{30000000000ULL}>();
    GossipPayloadBytes max_payload_bytes = ::fixy::mint_refined_constant<::fixy::positive, std::uint32_t{65507U}>();
    GossipReplication replication = GossipReplication::Hardware;
};

struct GossipMulticastSpec {
    dataplane::DeclaredXdpProgram program;
    dataplane::DeclaredBpfMap neighbor_map;
    GossipMulticastConfig config{};
};

using DeclaredGossipMulticastPlan = ::fixy::Tagged<GossipMulticastSpec, ::fixy::tags::source::GossipMulticast>;

// The replication of one packet: each neighbor of the topic gets a copy, and
// the program then ends the packet with the terminal action.
template <std::uint16_t MaxNeighbors>
    requires GossipNeighborShape<MaxNeighbors>
struct GossipReplicationPlan {
    DeclaredGossipTopic topic;
    IntegrityHash packet_id;
    GossipNeighborList<MaxNeighbors> neighbors{};
    dataplane::XdpAction terminal_action = dataplane::XdpAction::Drop;
};

// The plan mints an XDP program, so it needs what that mint needs, and its
// map takes the shape of a BPF map.
template <class Ctx, std::uint32_t MaxTopics, std::uint16_t MaxNeighbors>
concept CtxFitsGossipMulticastMint = dataplane::CtxFitsXdpMint<Ctx> && GossipMulticastShape<MaxTopics, MaxNeighbors>;

namespace detail {

// The xxHash64 of the characters of text.  The characters go to the hash in
// blocks of 32 bytes, so the hash is the hash of the whole run of bytes.
// Complexity: one pass over the characters.
[[nodiscard]] inline ::fixy::Result<IntegrityHash, IntegrityError> hash_text(::fixy::TextView text) noexcept {
    constexpr std::size_t kBlockBytes = 32;
    XxHash64State state = xxhash64_streaming();
    ::fixy::FixedArray<std::byte, kBlockBytes> block{};
    std::size_t held = 0;
    for (char const character : text) {
        block.set(::fixy::Index<kBlockBytes>::check(held).expect("a block that is not full has room"),
                  static_cast<std::byte>(static_cast<unsigned char>(character)));
        ++held;
        if (held == kBlockBytes) {
            CRUCIBLE_TRY(_, state.update(block.view()));
            held = 0;
        }
    }
    CRUCIBLE_TRY(_, state.update(block.view().window(0, held).expect("the held bytes fit the block")));
    return state.digest();
}

}  // namespace detail

[[nodiscard]] constexpr ::fixy::Result<DeclaredGossipTopic, GossipMulticastError>
admit_gossip_topic_hash(std::uint64_t hash) noexcept {
    if (hash == 0) {
        return ::fixy::err(GossipMulticastError::InvalidTopicHash);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::GossipMulticast>(GossipTopicKey{.hash = hash});
}

// The key of a topic is the xxHash64 of the characters of its name.
[[nodiscard]] inline ::fixy::Result<DeclaredGossipTopic, GossipMulticastError>
admit_gossip_topic(::fixy::TextView topic) noexcept {
    if (topic.size() == 0) {
        return ::fixy::err(GossipMulticastError::EmptyTopic);
    }
    using Admitted = ::fixy::Result<DeclaredGossipTopic, GossipMulticastError>;
    return detail::hash_text(topic).match(
        [](IntegrityHash hash) noexcept -> Admitted {
            return ::fixy::mint_tagged<::fixy::tags::source::GossipMulticast>(GossipTopicKey{.hash = hash.value()});
        },
        [](IntegrityError) noexcept -> Admitted { return ::fixy::err(GossipMulticastError::IntegrityHashFailed); });
}

[[nodiscard]] constexpr ::fixy::Result<GossipDedupWindowNs, GossipMulticastError>
admit_gossip_dedup_window_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, GossipMulticastError::InvalidDedupWindow);
}

[[nodiscard]] constexpr ::fixy::Result<GossipPayloadBytes, GossipMulticastError>
admit_gossip_payload_bytes(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, GossipMulticastError::InvalidPayloadLimit);
}

[[nodiscard]] constexpr GossipNeighborTarget gossip_neighbor_target(cog::CogIdentity const& peer,
                                                                    dataplane::XdpIfIndex ifindex,
                                                                    cog::sriov::MacAddress mac,
                                                                    Ipv4AddressBe ipv4) noexcept {
    return GossipNeighborTarget{peer.uuid, ifindex, mac, ipv4};
}

[[nodiscard]] constexpr dataplane::DeclaredXdpProgram
gossip_multicast_xdp_program(DeclaredGossipMulticastPlan const& plan) noexcept {
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
    DeclaredGossipMulticastPlan spec_;
    neighbor_map_type neighbors_{};

public:
    explicit constexpr GossipMulticastPlan(DeclaredGossipMulticastPlan spec) noexcept : spec_{spec} {}

    [[nodiscard]] constexpr DeclaredGossipMulticastPlan const& spec() const noexcept { return spec_; }

    [[nodiscard]] constexpr std::uint32_t topic_count() const noexcept { return neighbors_.size(); }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, GossipMulticastError>
    register_neighbor(DeclaredGossipTopic const& topic, GossipNeighborTarget const& target) noexcept {
        GossipTopicKey const key = topic.value();
        neighbor_list_type list = neighbors_.lookup(key).value_or(neighbor_list_type{});
        CRUCIBLE_TRY(_, list.push(target));
        if (neighbors_.update(key, list).is_err()) {
            return ::fixy::err(GossipMulticastError::TooManyTopics);
        }
        return ::fixy::Unit{};
    }

    [[nodiscard]] constexpr ::fixy::Option<neighbor_list_type>
    neighbors_for(DeclaredGossipTopic const& topic) const noexcept {
        return neighbors_.lookup(topic.value());
    }

    [[nodiscard]] ::fixy::Result<GossipReplicationPlan<MaxNeighbors>, GossipMulticastError>
    plan_packet(DeclaredGossipTopic const& topic, ::fixy::View<std::byte const> packet) const noexcept {
        if (packet.size() == 0) {
            return ::fixy::err(GossipMulticastError::EmptyPacket);
        }
        if (packet.size() > spec_.value().config.max_payload_bytes.value()) {
            return ::fixy::err(GossipMulticastError::PacketTooLarge);
        }
        CRUCIBLE_TRY(neighbors, neighbors_for(topic).ok_or(GossipMulticastError::UnknownTopic));
        using Planned = ::fixy::Result<GossipReplicationPlan<MaxNeighbors>, GossipMulticastError>;
        return xxhash64(packet).match(
            [&topic, &neighbors](IntegrityHash packet_id) noexcept -> Planned {
                return GossipReplicationPlan<MaxNeighbors>{
                    .topic = topic,
                    .packet_id = packet_id,
                    .neighbors = neighbors,
                    .terminal_action = dataplane::XdpAction::Drop,
                };
            },
            [](IntegrityError) noexcept -> Planned { return ::fixy::err(GossipMulticastError::IntegrityHashFailed); });
    }
};

template <std::uint32_t MaxTopics, std::uint16_t MaxNeighbors, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsGossipMulticastMint<Ctx, MaxTopics, MaxNeighbors>
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

}  // namespace crucible::cntp
