#include <crucible/cntp/GossipMulticast.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace dataplane = crucible::cntp::dataplane;
namespace source = ::fixy::tags::source;

namespace {

// The value of a call that succeeds in this test.
template <class Value, class Error>
Value take(::fixy::Result<Value, Error> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.
template <class Value, class Error>
Error error_of(::fixy::Result<Value, Error> result) {
    return std::move(result).err().expect("the call fails in this test");
}

// The order of a peer of the test: the low word of its UUID, the last octet
// of its MAC address and the last octet of its IPv4 address.
enum class PeerOrdinal : std::uint8_t {
};

[[nodiscard]] cntp::NicInterfaceName iface() {
    return cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
}

[[nodiscard]] cog::CogIdentity peer(PeerOrdinal ordinal) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x172, std::to_underlying(ordinal)};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

// The neighbor of the peer of ordinal, behind the interface ifindex.
[[nodiscard]] cntp::GossipNeighborTarget target(PeerOrdinal ordinal, dataplane::XdpIfIndex ifindex) {
    std::uint8_t const octet = std::to_underlying(ordinal);
    return cntp::gossip_neighbor_target(peer(ordinal), ifindex, cog::sriov::MacAddress::locally_administered(octet),
                                        cntp::Ipv4AddressBe{0x0A000000u | octet});
}

[[nodiscard]] dataplane::XdpIfIndex ifindex_of(PeerOrdinal ordinal) {
    return take(dataplane::admit_xdp_ifindex(10u + std::to_underlying(ordinal)));
}

// The one-shot xxHash64 of the Count characters of name, read as one run of
// bytes.  Complexity: one pass over the characters.
template <std::size_t Count>
[[nodiscard]] std::uint64_t hash_of_bytes(::fixy::TextView name) {
    CRUCIBLE_TEST_ASSERT(kTest, name.size() == Count);
    ::fixy::FixedArray<std::byte, Count> bytes{};
    std::size_t position = 0;
    for (char const character : name) {
        *bytes.get(position).expect("a test reads an element at or past the count of its array") =
            static_cast<std::byte>(static_cast<unsigned char>(character));
        ++position;
    }
    ::fixy::View<std::byte const> const run = std::as_const(bytes).view();
    return take(cntp::xxhash64(run)).value();
}

void test_admission() {
    CRUCIBLE_TEST_ASSERT(kTest, cntp::gossip_multicast_error_name(cntp::GossipMulticastError::DuplicateNeighbor)
                                    == "DuplicateNeighbor");
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_gossip_topic("")) == cntp::GossipMulticastError::EmptyTopic);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::admit_gossip_topic_hash(0)) == cntp::GossipMulticastError::InvalidTopicHash);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_gossip_dedup_window_ns(0))
                                    == cntp::GossipMulticastError::InvalidDedupWindow);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_gossip_payload_bytes(0))
                                    == cntp::GossipMulticastError::InvalidPayloadLimit);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_gossip_topic("canopy.delta").is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, take(cntp::admit_gossip_topic_hash(0x172)).value().hash == 0x172u);

    // The key of a topic is the xxHash64 of the whole run of the bytes of its
    // name: for a name shorter than one block of 32 characters, for a name of
    // exactly one block and for a name past one block.
    ::fixy::TextView const short_name = "canopy.delta";
    ::fixy::TextView const block_name = "canopy.delta.of.thirty-two.chars";
    ::fixy::TextView const long_name = "canopy.delta.of.a.topic.with.a.name.of.more.than.one.block";
    std::uint64_t const short_hash = hash_of_bytes<12>(short_name);
    std::uint64_t const block_hash = hash_of_bytes<32>(block_name);
    std::uint64_t const long_hash = hash_of_bytes<58>(long_name);
    CRUCIBLE_TEST_ASSERT(kTest, take(cntp::admit_gossip_topic(short_name)).value().hash == short_hash);
    CRUCIBLE_TEST_ASSERT(kTest, take(cntp::admit_gossip_topic(block_name)).value().hash == block_hash);
    CRUCIBLE_TEST_ASSERT(kTest, take(cntp::admit_gossip_topic(long_name)).value().hash == long_hash);
    CRUCIBLE_TEST_ASSERT(kTest, short_hash != block_hash && block_hash != long_hash && short_hash != long_hash);

    cntp::GossipNeighborTarget const neighbor = target(PeerOrdinal{9}, ifindex_of(PeerOrdinal{11}));
    CRUCIBLE_TEST_ASSERT(kTest, neighbor.ifindex().value() == 21u);
    CRUCIBLE_TEST_ASSERT(kTest, neighbor.peer() == peer(PeerOrdinal{9}).uuid);
    CRUCIBLE_TEST_ASSERT(kTest, neighbor.ipv4() == cntp::Ipv4AddressBe{0x0A000009u});
    CRUCIBLE_TEST_ASSERT(kTest, neighbor.mac().bytes == cog::sriov::MacAddress::locally_administered(9).bytes);
    static_assert(cntp::GossipNeighborTarget{}.ifindex().value() == 1u);

    ::fixy::check::pass(kTest, "  test_admission: PASSED\n");
}

void test_plan_registration_and_publish() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto plan = cntp::mint_gossip_multicast_plan<2, 2>(
        init, iface(), ifindex_of(PeerOrdinal{0}),
        cntp::GossipMulticastConfig{
            .dedup_window_ns = take(cntp::admit_gossip_dedup_window_ns(30000000000ULL)),
            .max_payload_bytes = take(cntp::admit_gossip_payload_bytes(128)),
            .replication = cntp::GossipReplication::Hardware,
        });
    static_assert(!std::copy_constructible<decltype(plan)>);
    static_assert(!std::move_constructible<decltype(plan)>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(plan.spec())>::tag_type, source::GossipMulticast>);

    auto const xdp = cntp::gossip_multicast_xdp_program(plan.spec());
    CRUCIBLE_TEST_ASSERT(kTest, xdp.value().kind == dataplane::XdpProgramKind::GossipMulticast);
    CRUCIBLE_TEST_ASSERT(kTest, xdp.value().required_features.test(cog::NicFeature::XdpNative));
    dataplane::BpfMapSpec const& map = plan.spec().value().neighbor_map.value();
    CRUCIBLE_TEST_ASSERT(kTest, map.kind == dataplane::BpfMapKind::LruHash);
    CRUCIBLE_TEST_ASSERT(kTest, map.key_bytes.value() == sizeof(cntp::GossipTopicKey));
    CRUCIBLE_TEST_ASSERT(kTest, map.value_bytes.value() == sizeof(cntp::GossipNeighborList<2>));
    CRUCIBLE_TEST_ASSERT(kTest, map.max_entries.value() == 2u);

    cntp::DeclaredGossipTopic const topic = take(cntp::admit_gossip_topic("canopy.delta"));
    cntp::DeclaredGossipTopic const other = take(cntp::admit_gossip_topic("federation.ack"));

    CRUCIBLE_TEST_ASSERT(kTest,
                         plan.register_neighbor(topic, target(PeerOrdinal{1}, ifindex_of(PeerOrdinal{1}))).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         plan.register_neighbor(topic, target(PeerOrdinal{2}, ifindex_of(PeerOrdinal{2}))).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(plan.register_neighbor(topic, target(PeerOrdinal{2}, ifindex_of(PeerOrdinal{3}))))
                             == cntp::GossipMulticastError::DuplicateNeighbor);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(plan.register_neighbor(topic, target(PeerOrdinal{3}, ifindex_of(PeerOrdinal{3}))))
                             == cntp::GossipMulticastError::TooManyNeighbors);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(plan.register_neighbor(topic, cntp::GossipNeighborTarget{}))
                                    == cntp::GossipMulticastError::InvalidPeer);

    CRUCIBLE_TEST_ASSERT(kTest,
                         plan.register_neighbor(other, target(PeerOrdinal{4}, ifindex_of(PeerOrdinal{4}))).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, plan.topic_count() == 2u);
    cntp::DeclaredGossipTopic const third = take(cntp::admit_gossip_topic("third.topic"));
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(plan.register_neighbor(third, target(PeerOrdinal{5}, ifindex_of(PeerOrdinal{5}))))
                             == cntp::GossipMulticastError::TooManyTopics);

    ::fixy::FixedArray<std::byte, 4> const packet{std::byte{0xC1}, std::byte{0x7A}, std::byte{0x01}, std::byte{0x72}};
    auto const replication = take(plan.plan_packet(topic, packet.view()));
    CRUCIBLE_TEST_ASSERT(kTest, replication.neighbors.count() == 2u);
    CRUCIBLE_TEST_ASSERT(kTest, replication.neighbors.contains(peer(PeerOrdinal{1}).uuid));
    CRUCIBLE_TEST_ASSERT(kTest, replication.neighbors.contains(peer(PeerOrdinal{2}).uuid));
    CRUCIBLE_TEST_ASSERT(kTest, replication.packet_id.value() != 0u);
    CRUCIBLE_TEST_ASSERT(kTest, replication.terminal_action == dataplane::XdpAction::Drop);

    cntp::DeclaredGossipTopic const unknown = take(cntp::admit_gossip_topic("unknown.topic"));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(plan.plan_packet(unknown, packet.view()))
                                    == cntp::GossipMulticastError::UnknownTopic);

    ::fixy::FixedArray<std::byte, 129> const oversized{};
    CRUCIBLE_TEST_ASSERT(kTest, error_of(plan.plan_packet(topic, oversized.view()))
                                    == cntp::GossipMulticastError::PacketTooLarge);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(plan.plan_packet(topic, packet.view().window(0, 0).expect("an empty window fits")))
                             == cntp::GossipMulticastError::EmptyPacket);

    ::fixy::check::pass(kTest, "  test_plan_registration_and_publish: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::DeclaredGossipTopic) == sizeof(cntp::GossipTopicKey));
    static_assert(dataplane::BpfKey<cntp::GossipTopicKey>);
    static_assert(dataplane::BpfScalar<cntp::GossipNeighborTarget>);
    static_assert(cntp::GossipMulticastShape<2, 2>);
    static_assert(!cntp::GossipMulticastShape<0, 2>);
    static_assert(!cntp::GossipMulticastShape<2, 0>);
    static_assert(cntp::CtxFitsGossipMulticastMint<::fixy::ColdInitCtx, 2, 2>);
    static_assert(!cntp::CtxFitsGossipMulticastMint<::fixy::BgDrainCtx, 2, 2>);
    static_assert(!cntp::CtxFitsGossipMulticastMint<::fixy::HotFgCtx, 2, 2>);
    static_assert(!cntp::CtxFitsGossipMulticastMint<::fixy::ColdInitCtx, 2, 0>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_gossip_multicast:\n");
    test_admission();
    test_plan_registration_and_publish();
    ::fixy::check::pass(kTest, "test_cntp_gossip_multicast: all PASSED\n");
    return 0;
}
