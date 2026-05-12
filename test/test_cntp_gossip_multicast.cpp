#include <crucible/cntp/GossipMulticast.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace rt = crucible::rt;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cntp::NicInterfaceName iface() {
    auto parsed = cntp::NicInterfaceName::from("eth0");
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x172, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] rt::XdpIfIndex ifindex(std::uint32_t value) {
    auto admitted = rt::admit_xdp_ifindex(value);
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cntp::GossipNeighborTarget target(std::uint64_t uuid_lo,
                                                std::uint32_t ifidx) {
    return cntp::gossip_neighbor_target(
        peer(uuid_lo),
        ifindex(ifidx),
        std::array<std::byte, 6>{
            std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            static_cast<std::byte>(uuid_lo & 0xFFU)},
        0x0A00'0000U | static_cast<std::uint32_t>(uuid_lo));
}

void test_admission() {
    assert(cntp::gossip_multicast_error_name(
               cntp::GossipMulticastError::DuplicateNeighbor) ==
           std::string_view{"DuplicateNeighbor"});
    assert(!cntp::admit_gossip_topic("").has_value());
    assert(!cntp::admit_gossip_topic_hash(0).has_value());
    assert(!cntp::admit_gossip_dedup_window_ns(0).has_value());
    assert(!cntp::admit_gossip_payload_bytes(0).has_value());
    assert(cntp::admit_gossip_topic("canopy.delta").has_value());
    auto explicit_hash = cntp::admit_gossip_topic_hash(0x172);
    assert(explicit_hash.has_value());
    assert(explicit_hash->value().hash == 0x172);

    std::printf("  test_admission: PASSED\n");
}

void test_plan_registration_and_publish() {
    effects::ColdInitCtx init{};
    auto bytes = cntp::admit_gossip_payload_bytes(128);
    auto window = cntp::admit_gossip_dedup_window_ns(30'000'000'000ULL);
    assert(bytes.has_value());
    assert(window.has_value());

    auto plan = cntp::mint_gossip_multicast_plan<2, 2>(
        init,
        iface(),
        ifindex(7),
        cntp::GossipMulticastConfig{
            .dedup_window_ns = *window,
            .max_payload_bytes = *bytes,
            .use_hardware_replication = true,
        });
    static_assert(!std::copy_constructible<decltype(plan)>);
    static_assert(!std::move_constructible<decltype(plan)>);
    static_assert(std::same_as<
                  decltype(plan.spec())::tag_type,
                  saf::source::GossipMulticast>);

    auto xdp = cntp::gossip_multicast_xdp_program(plan.spec());
    assert(xdp.value().kind == rt::XdpProgramKind::GossipMulticast);
    assert(xdp.value().required_features.test(cog::NicFeature::XdpNative));
    assert(plan.spec().value().neighbor_map.value().kind ==
           rt::BpfMapKind::LruHash);

    auto topic = cntp::admit_gossip_topic("canopy.delta");
    auto other = cntp::admit_gossip_topic("federation.ack");
    assert(topic.has_value());
    assert(other.has_value());

    assert(plan.register_neighbor(*topic, target(1, 10)).has_value());
    assert(plan.register_neighbor(*topic, target(2, 11)).has_value());
    auto duplicate = plan.register_neighbor(*topic, target(2, 12));
    assert(!duplicate.has_value());
    assert(duplicate.error() == cntp::GossipMulticastError::DuplicateNeighbor);
    auto full = plan.register_neighbor(*topic, target(3, 13));
    assert(!full.has_value());
    assert(full.error() == cntp::GossipMulticastError::TooManyNeighbors);

    assert(plan.register_neighbor(*other, target(4, 14)).has_value());
    assert(plan.topic_count() == 2);

    std::array<std::byte, 4> packet{
        std::byte{0xC1}, std::byte{0x7A},
        std::byte{0x01}, std::byte{0x72}};
    auto replication = plan.plan_packet(*topic, std::span<const std::byte>{packet});
    assert(replication.has_value());
    assert(replication->neighbors.count == 2);
    assert(replication->packet_id.value() != 0);
    assert(replication->terminal_action == rt::XdpAction::Drop);

    auto unknown = cntp::admit_gossip_topic("unknown.topic");
    assert(unknown.has_value());
    auto missing = plan.plan_packet(*unknown, std::span<const std::byte>{packet});
    assert(!missing.has_value());
    assert(missing.error() == cntp::GossipMulticastError::UnknownTopic);

    std::array<std::byte, 129> oversized{};
    auto too_large = plan.plan_packet(*topic, std::span<const std::byte>{oversized});
    assert(!too_large.has_value());
    assert(too_large.error() == cntp::GossipMulticastError::PacketTooLarge);

    std::printf("  test_plan_registration_and_publish: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::GossipTopicHash) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredGossipTopic) ==
                  sizeof(cntp::GossipTopicKey));
    static_assert(rt::BpfKey<cntp::GossipTopicKey>);
    static_assert(rt::BpfScalar<cntp::GossipNeighborTarget>);
    static_assert(cntp::GossipMulticastShape<2, 2>);
    static_assert(!cntp::GossipMulticastShape<0, 2>);
    static_assert(!cntp::GossipMulticastShape<2, 0>);
    static_assert(cntp::CtxFitsGossipMulticastMint<effects::ColdInitCtx>);
    static_assert(!cntp::CtxFitsGossipMulticastMint<effects::BgDrainCtx>);

    std::printf("test_cntp_gossip_multicast:\n");
    test_admission();
    test_plan_registration_and_publish();
    std::printf("test_cntp_gossip_multicast: all PASSED\n");
    return 0;
}
