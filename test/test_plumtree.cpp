#include <crucible/canopy/Plumtree.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

[[nodiscard]] crucible::cog::CogIdentity peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 1200};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

[[nodiscard]] crucible::canopy::HyParViewPeer hp(
    std::uint64_t id) noexcept {
    auto admitted = crucible::canopy::admit_hyparview_peer(peer(id));
    assert(admitted.has_value());
    return *admitted;
}

}  // namespace

int main() {
    namespace cc = crucible::canopy;

    using Broadcast = cc::PlumtreeBroadcast<4, 8>;
    static_assert(!std::is_copy_constructible_v<Broadcast>);
    static_assert(!std::is_move_constructible_v<Broadcast>);
    static_assert(alignof(Broadcast) >= 64);
    static_assert(std::same_as<cc::PlumtreeMessageId::tag_type,
                               crucible::safety::source::Plumtree>);

    assert(cc::plumtree_error_name(cc::PlumtreeError::UnknownPeer) ==
           std::string_view{"UnknownPeer"});

    std::array active{hp(1), hp(2), hp(3)};
    cc::HyParViewConfig hy_config{
        .active_size = cc::HyParViewPositiveCount{3},
        .passive_size = cc::HyParViewPositiveCount{4},
        .active_random_walk_length = cc::HyParViewPositiveCount{3},
        .passive_random_walk_length = cc::HyParViewPositiveCount{2},
        .active_random_walk_acceptance = cc::HyParViewPositiveCount{2},
        .shuffle_period_ns = cc::HyParViewDurationNs{30'000'000'000ULL},
    };
    auto membership = cc::mint_hyparview<3, 4>(
        crucible::effects::testing::init(),
        std::span<const cc::HyParViewPeer>{active},
        {},
        hy_config);

    cc::PlumtreeConfig config{
        .ihave_timeout_ns = cc::PlumtreeDurationNs{100'000'000ULL},
        .repair_timeout_ns = cc::PlumtreeDurationNs{200'000'000ULL},
        .lazy_push_period_ns = cc::PlumtreeDurationNs{100'000'000ULL},
        .max_eager_fanout = cc::PlumtreePositiveCount{2},
    };
    auto broadcast = cc::mint_plumtree<4, 8>(
        crucible::effects::testing::init(),
        membership,
        config);

    assert(broadcast.link_count().value() == 3);
    assert(broadcast.eager_count().value() == 2);
    assert(broadcast.lazy_count().value() == 1);
    assert(broadcast.add_lazy_peer(hp(4)).has_value());
    assert(broadcast.link_count().value() == 4);

    auto duplicate = broadcast.add_eager_peer(hp(4));
    assert(!duplicate.has_value());
    assert(duplicate.error() == cc::PlumtreeError::DuplicatePeer);

    std::array<std::byte, 5> payload{
        std::byte{0x70},
        std::byte{0x6c},
        std::byte{0x75},
        std::byte{0x6d},
        std::byte{0x21},
    };
    auto id = cc::plumtree_message_id(std::span<const std::byte>{payload});
    assert(id.has_value());

    auto empty = cc::plumtree_message_id(std::span<const std::byte>{});
    assert(!empty.has_value());
    assert(empty.error() == cc::PlumtreeError::EmptyMessage);

    auto published = broadcast.publish(std::span<const std::byte>{payload});
    assert(published.has_value());
    assert(published->message.id_hash == cc::plumtree_message_hash(*id));
    assert(published->message.payload_bytes == payload.size());
    assert(published->eager_count == 2);
    assert(published->lazy_count == 2);
    assert(published->eager_size().value() == 2);
    assert(broadcast.history_size().value() == 1);

    auto summary = broadcast.ihave_summary();
    assert(summary.size().value() == 1);
    assert(summary.ids[0] == cc::plumtree_message_hash(*id));

    cc::PlumtreeMessage remote_msg{
        .id_hash = cc::plumtree_message_hash(*id),
        .payload_bytes = static_cast<std::uint32_t>(payload.size()),
    };
    auto duplicate_receive = broadcast.receive_message(
        hp(1),
        cc::GossipedPlumtreeMessage{remote_msg});
    assert(duplicate_receive.has_value());
    assert(duplicate_receive->kind == cc::PlumtreeReceiveKind::Duplicate);
    assert(broadcast.link_state(peer(1).uuid).value() ==
           cc::PlumtreeLinkState::Lazy);

    std::array<std::byte, 4> other_payload{
        std::byte{0x44},
        std::byte{0x41},
        std::byte{0x54},
        std::byte{0x41},
    };
    auto other_id = cc::plumtree_message_id(
        std::span<const std::byte>{other_payload});
    assert(other_id.has_value());
    cc::PlumtreeMessage unseen_msg{
        .id_hash = cc::plumtree_message_hash(*other_id),
        .payload_bytes = static_cast<std::uint32_t>(other_payload.size()),
    };
    auto first_receive = broadcast.receive_message(
        hp(4),
        cc::GossipedPlumtreeMessage{unseen_msg});
    assert(first_receive.has_value());
    assert(first_receive->kind == cc::PlumtreeReceiveKind::FirstSeen);
    assert(first_receive->forward.eager_count == 1);
    assert(first_receive->forward.lazy_count == 2);
    assert(broadcast.history_size().value() == 2);

    cc::PlumtreeIHave<8> ihave{};
    ihave.ids[0] = cc::plumtree_message_hash(*other_id);
    auto missing_payload = std::array<std::byte, 3>{
        std::byte{0x6e},
        std::byte{0x65},
        std::byte{0x77},
    };
    auto missing_id = cc::plumtree_message_id(
        std::span<const std::byte>{missing_payload});
    assert(missing_id.has_value());
    ihave.ids[1] = cc::plumtree_message_hash(*missing_id);
    ihave.count = 2;
    auto repair = broadcast.receive_ihave(
        hp(2),
        cc::GossipedPlumtreeIHave<8>{ihave});
    assert(repair.has_value());
    assert(repair->source.uuid == peer(2).uuid);
    assert(repair->size().value() == 1);
    assert(repair->requested[0] == cc::plumtree_message_hash(*missing_id));

    auto promote_payload = std::array<std::byte, 4>{
        std::byte{0x70},
        std::byte{0x72},
        std::byte{0x6f},
        std::byte{0x6d},
    };
    auto promote_id = cc::plumtree_message_id(
        std::span<const std::byte>{promote_payload});
    assert(promote_id.has_value());
    cc::PlumtreeIHave<8> promote_ihave{};
    promote_ihave.ids[0] = cc::plumtree_message_hash(*promote_id);
    promote_ihave.count = 1;
    auto promoted_repair = broadcast.receive_ihave(
        hp(3),
        cc::GossipedPlumtreeIHave<8>{promote_ihave});
    assert(promoted_repair.has_value());
    assert(promoted_repair->size().value() == 1);
    assert(broadcast.link_state(peer(3).uuid).value() ==
           cc::PlumtreeLinkState::Eager);
    assert(broadcast.eager_count().value() ==
           broadcast.config().max_eager_fanout.value());

    auto unknown = broadcast.receive_ihave(
        hp(99),
        cc::GossipedPlumtreeIHave<8>{ihave});
    assert(!unknown.has_value());
    assert(unknown.error() == cc::PlumtreeError::UnknownPeer);

    // fixy-A5-030 regression: recoverable admission path must reject
    // over-capacity Plumtree config and over-capacity membership WITHOUT
    // aborting the process via CRUCIBLE_FATAL_INVARIANT.  Pre-fix the
    // only way to construct from untrusted input was the trapping
    // ctor; now callers can pre-validate and recover.

    // Config too large for template params: max_eager_fanout > MaxPeers.
    {
        cc::PlumtreeConfig too_big{
            .ihave_timeout_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .repair_timeout_ns = cc::PlumtreeDurationNs{200'000'000ULL},
            .lazy_push_period_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .max_eager_fanout = cc::PlumtreePositiveCount{99},
        };
        auto admitted = cc::admit_plumtree_config<4>(too_big);
        assert(!admitted.has_value());
        assert(admitted.error() == cc::PlumtreeError::InvalidConfig);
    }

    // Config valid → returns the config unchanged.
    {
        cc::PlumtreeConfig ok{
            .ihave_timeout_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .repair_timeout_ns = cc::PlumtreeDurationNs{200'000'000ULL},
            .lazy_push_period_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .max_eager_fanout = cc::PlumtreePositiveCount{2},
        };
        auto admitted = cc::admit_plumtree_config<4>(ok);
        assert(admitted.has_value());
        assert(admitted->max_eager_fanout.value() == 2);
    }

    // Membership active view larger than Plumtree MaxPeers →
    // CapacityExceeded (would have trapped pre-fix via the membership
    // ctor's CRUCIBLE_FATAL_INVARIANT on the per-peer add_link_).
    {
        std::array big_active{hp(60), hp(61), hp(62), hp(63), hp(64)};
        cc::HyParViewConfig big_hy{
            .active_size = cc::HyParViewPositiveCount{5},
            .passive_size = cc::HyParViewPositiveCount{6},
            .active_random_walk_length = cc::HyParViewPositiveCount{3},
            .passive_random_walk_length = cc::HyParViewPositiveCount{2},
            .active_random_walk_acceptance = cc::HyParViewPositiveCount{2},
            .shuffle_period_ns = cc::HyParViewDurationNs{30'000'000'000ULL},
        };
        auto big_membership = cc::mint_hyparview<5, 6>(
            crucible::effects::testing::init(),
            std::span<const cc::HyParViewPeer>{big_active},
            {},
            big_hy);

        cc::PlumtreeConfig small_pt{
            .ihave_timeout_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .repair_timeout_ns = cc::PlumtreeDurationNs{200'000'000ULL},
            .lazy_push_period_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .max_eager_fanout = cc::PlumtreePositiveCount{2},
        };
        auto admitted = cc::admit_plumtree_config<3>(small_pt);
        assert(admitted.has_value());
        cc::PlumtreeBroadcast<3, 8> b{*admitted};
        auto rc = cc::populate_plumtree_from_membership(b, big_membership);
        assert(!rc.has_value());
        assert(rc.error() == cc::PlumtreeError::CapacityExceeded);
        // Broadcast stays empty — early reject avoided partial fill.
        assert(b.link_count().value() == 0);
    }

    // Happy path: small membership + valid config → succeeds.
    {
        std::array good_active{hp(70), hp(71)};
        cc::HyParViewConfig good_hy{
            .active_size = cc::HyParViewPositiveCount{2},
            .passive_size = cc::HyParViewPositiveCount{4},
            .active_random_walk_length = cc::HyParViewPositiveCount{3},
            .passive_random_walk_length = cc::HyParViewPositiveCount{2},
            .active_random_walk_acceptance = cc::HyParViewPositiveCount{2},
            .shuffle_period_ns = cc::HyParViewDurationNs{30'000'000'000ULL},
        };
        auto good_membership = cc::mint_hyparview<2, 4>(
            crucible::effects::testing::init(),
            std::span<const cc::HyParViewPeer>{good_active},
            {},
            good_hy);

        cc::PlumtreeConfig good_pt{
            .ihave_timeout_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .repair_timeout_ns = cc::PlumtreeDurationNs{200'000'000ULL},
            .lazy_push_period_ns = cc::PlumtreeDurationNs{100'000'000ULL},
            .max_eager_fanout = cc::PlumtreePositiveCount{2},
        };
        auto admitted = cc::admit_plumtree_config<4>(good_pt);
        assert(admitted.has_value());
        cc::PlumtreeBroadcast<4, 8> b{*admitted};
        auto rc = cc::populate_plumtree_from_membership(b, good_membership);
        assert(rc.has_value());
        assert(b.link_count().value() == 2);
        assert(b.eager_count().value() == 2);
    }

    return 0;
}
