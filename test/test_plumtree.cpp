#include <crucible/canopy/Plumtree.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include "test_assert.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace {

namespace cc = crucible::canopy;

[[nodiscard]] crucible::cog::CogIdentity peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 1200};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

[[nodiscard]] cc::HyParViewPeer hp(std::uint64_t id) noexcept {
    auto admitted = cc::admit_hyparview_peer(peer(id));
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cc::HyParViewPositiveCount positive(std::uint16_t count) noexcept {
    return ::fixy::mint_refined<::fixy::positive>(count);
}

// An overlay config with room for `active` active peers.
[[nodiscard]] cc::HyParViewConfig overlay_config(std::uint16_t active, std::uint16_t passive) noexcept {
    return cc::HyParViewConfig{
        .active_size = positive(active),
        .passive_size = positive(passive),
        .active_random_walk_length = positive(3),
        .passive_random_walk_length = positive(2),
        .active_random_walk_acceptance = positive(2),
    };
}

// A broadcast config with an eager fanout of `fanout`.
[[nodiscard]] cc::PlumtreeConfig broadcast_config(std::uint16_t fanout) noexcept {
    return cc::PlumtreeConfig{.max_eager_fanout = positive(fanout)};
}

[[nodiscard]] cc::GossipedPlumtreeIHave<8> gossiped(cc::PlumtreeIHave<8> ihave) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Gossiped>(ihave);
}

[[nodiscard]] cc::GossipedPlumtreeMessage gossiped(cc::PlumtreeMessage message) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Gossiped>(message);
}

}  // namespace

int main() {
    using Broadcast = cc::PlumtreeBroadcast<4, 8>;
    static_assert(!std::is_default_constructible_v<Broadcast>);
    static_assert(!std::is_copy_constructible_v<Broadcast>);
    static_assert(!std::is_move_constructible_v<Broadcast>);
    static_assert(alignof(Broadcast) >= 64);
    static_assert(std::same_as<cc::PlumtreeMessageId::tag_type, ::fixy::tags::source::Plumtree>);

    static_assert(::foundation::reflect::enum_name(cc::PlumtreeError::UnknownPeer) == "UnknownPeer");

    // A tree needs a link slot for each peer that the active view can hold.
    static_assert(cc::PlumtreeFitsOverlay<4, 8, 3, 4>);
    static_assert(cc::PlumtreeFitsOverlay<4, 8, 4, 8>);
    static_assert(!cc::PlumtreeFitsOverlay<3, 8, 5, 6>);

    std::array active{hp(1), hp(2), hp(3)};
    auto membership = cc::mint_hyparview<3, 4>(::foundation::effects::testing::init(),
                                               std::span<const cc::HyParViewPeer>{active}, {}, overlay_config(3, 4));

    auto broadcast = cc::mint_plumtree<4, 8>(::foundation::effects::testing::init(), membership, broadcast_config(2));

    assert(broadcast.link_count().value() == 3);
    assert(broadcast.eager_count().value() == 2);
    assert(broadcast.lazy_count().value() == 1);
    assert(broadcast.add_lazy_peer(hp(4)).has_value());
    assert(broadcast.link_count().value() == 4);

    auto duplicate = broadcast.add_eager_peer(hp(4));
    assert(!duplicate.has_value());
    assert(duplicate.error() == cc::PlumtreeError::DuplicatePeer);

    auto full = broadcast.add_lazy_peer(hp(5));
    assert(!full.has_value());
    assert(full.error() == cc::PlumtreeError::CapacityExceeded);

    std::array<std::byte, 5> payload{
        std::byte{0x70}, std::byte{0x6c}, std::byte{0x75}, std::byte{0x6d}, std::byte{0x21},
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
    assert(published->eager_peers.count == 2);
    assert(published->lazy_peers.count == 2);
    assert(published->eager_peers.size().value() == 2);
    assert(broadcast.history_size().value() == 1);

    auto summary = broadcast.ihave_summary();
    assert(summary.size().value() == 1);
    assert(summary.slots[0] == cc::plumtree_message_hash(*id));

    cc::PlumtreeMessage remote_msg{
        .id_hash = cc::plumtree_message_hash(*id),
        .payload_bytes = static_cast<std::uint32_t>(payload.size()),
    };
    auto duplicate_receive = broadcast.receive_message(hp(1), gossiped(remote_msg));
    assert(duplicate_receive.has_value());
    assert(duplicate_receive->kind == cc::PlumtreeReceiveKind::Duplicate);
    assert(broadcast.link_state(peer(1).uuid).value() == cc::PlumtreeLinkState::Lazy);

    std::array<std::byte, 4> other_payload{
        std::byte{0x44},
        std::byte{0x41},
        std::byte{0x54},
        std::byte{0x41},
    };
    auto other_id = cc::plumtree_message_id(std::span<const std::byte>{other_payload});
    assert(other_id.has_value());
    cc::PlumtreeMessage unseen_msg{
        .id_hash = cc::plumtree_message_hash(*other_id),
        .payload_bytes = static_cast<std::uint32_t>(other_payload.size()),
    };
    auto first_receive = broadcast.receive_message(hp(4), gossiped(unseen_msg));
    assert(first_receive.has_value());
    assert(first_receive->kind == cc::PlumtreeReceiveKind::FirstSeen);
    assert(first_receive->forward.eager_peers.count == 1);
    assert(first_receive->forward.lazy_peers.count == 2);
    assert(broadcast.history_size().value() == 2);

    auto unknown_sender = broadcast.receive_message(hp(98), gossiped(unseen_msg));
    assert(!unknown_sender.has_value());
    assert(unknown_sender.error() == cc::PlumtreeError::UnknownPeer);

    cc::PlumtreeIHave<8> ihave{};
    assert(ihave.push(cc::plumtree_message_hash(*other_id)));
    auto missing_payload = std::array<std::byte, 3>{
        std::byte{0x6e},
        std::byte{0x65},
        std::byte{0x77},
    };
    auto missing_id = cc::plumtree_message_id(std::span<const std::byte>{missing_payload});
    assert(missing_id.has_value());
    assert(ihave.push(cc::plumtree_message_hash(*missing_id)));
    auto repair = broadcast.receive_ihave(hp(2), gossiped(ihave));
    assert(repair.has_value());
    assert(repair->source.uuid == peer(2).uuid);
    assert(repair->requested.size().value() == 1);
    assert(repair->requested.slots[0] == cc::plumtree_message_hash(*missing_id));

    auto promote_payload = std::array<std::byte, 4>{
        std::byte{0x70},
        std::byte{0x72},
        std::byte{0x6f},
        std::byte{0x6d},
    };
    auto promote_id = cc::plumtree_message_id(std::span<const std::byte>{promote_payload});
    assert(promote_id.has_value());
    cc::PlumtreeIHave<8> promote_ihave{};
    assert(promote_ihave.push(cc::plumtree_message_hash(*promote_id)));
    auto promoted_repair = broadcast.receive_ihave(hp(3), gossiped(promote_ihave));
    assert(promoted_repair.has_value());
    assert(promoted_repair->requested.size().value() == 1);
    assert(broadcast.link_state(peer(3).uuid).value() == cc::PlumtreeLinkState::Eager);
    assert(broadcast.eager_count().value() == broadcast.config().max_eager_fanout.value());

    auto unknown = broadcast.receive_ihave(hp(99), gossiped(ihave));
    assert(!unknown.has_value());
    assert(unknown.error() == cc::PlumtreeError::UnknownPeer);

    // Admission rejects an eager fanout that exceeds the link slots by
    // returning an error rather than by stopping the process.  A caller
    // handed untrusted input needs a way to check it first.
    {
        auto admitted = cc::admit_plumtree_config<4>(broadcast_config(99));
        assert(!admitted.has_value());
        assert(admitted.error() == cc::PlumtreeError::InvalidConfig);
    }

    {
        auto admitted = cc::admit_plumtree_config<4>(broadcast_config(2));
        assert(admitted.has_value());
        assert(admitted->max_eager_fanout.value() == 2);
    }

    // An overlay whose active view fills every link slot: each active peer
    // becomes a link, the first two eager and the rest lazy.
    {
        std::array full_active{hp(70), hp(71), hp(72), hp(73)};
        auto full_membership =
            cc::mint_hyparview<4, 8>(::foundation::effects::testing::init(),
                                     std::span<const cc::HyParViewPeer>{full_active}, {}, overlay_config(4, 8));
        auto b = cc::mint_plumtree<4, 8>(::foundation::effects::testing::init(), full_membership, broadcast_config(2));
        assert(b.link_count().value() == 4);
        assert(b.eager_count().value() == 2);
        assert(b.lazy_count().value() == 2);
    }

    // An empty overlay mints an empty tree.
    {
        auto empty_membership =
            cc::mint_hyparview<2, 4>(::foundation::effects::testing::init(), {}, {}, overlay_config(2, 4));
        auto b = cc::mint_plumtree<4, 8>(::foundation::effects::testing::init(), empty_membership, broadcast_config(2));
        assert(b.link_count().value() == 0);
        assert(b.eager_count().value() == 0);
    }

    return 0;
}
