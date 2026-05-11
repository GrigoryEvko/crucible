#include <crucible/canopy/Swim.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace {

[[nodiscard]] crucible::cog::CogIdentity
peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 100};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

}  // namespace

int main() {
    using crucible::canopy::GossipedSwimEvent;
    using crucible::canopy::SwimConfig;
    using crucible::canopy::SwimError;
    using crucible::canopy::SwimMembership;
    using crucible::canopy::SwimPeer;
    using crucible::canopy::SwimState;
    using crucible::canopy::admit_swim_peer;
    using crucible::canopy::mint_swim_membership;

    static_assert(!std::is_copy_constructible_v<SwimMembership<8>>);
    static_assert(!std::is_move_constructible_v<SwimMembership<8>>);
    static_assert(alignof(SwimMembership<8>) >= 64);

    std::array<SwimPeer, 3> initial{
        admit_swim_peer(peer(1)),
        admit_swim_peer(peer(2)),
        admit_swim_peer(peer(3)),
    };

    auto membership = mint_swim_membership<8>(
        crucible::effects::Init{},
        std::span<const SwimPeer>{initial});
    assert(membership.size().value() == 3);
    assert(membership.config().period_ns.value() ==
           SwimConfig{}.period_ns.value());

    auto live = membership.live_peers();
    assert(live.size() == 3);
    assert(live[0].uuid == peer(1).uuid);
    assert(live[1].uuid == peer(2).uuid);
    assert(live[2].uuid == peer(3).uuid);

    auto duplicate = membership.add_peer(admit_swim_peer(peer(1)));
    assert(!duplicate.has_value());
    assert(duplicate.error() == SwimError::DuplicatePeer);

    auto zero = membership.add_peer(admit_swim_peer(crucible::cog::CogIdentity{}));
    assert(!zero.has_value());
    assert(zero.error() == SwimError::ZeroUuid);

    auto probe = membership.next_probe(1'000);
    assert(probe.has_value());
    assert(probe->target == peer(1).uuid);
    assert(probe->deadline_ns == 500'001'000ULL);

    assert(membership.on_ping_timeout(peer(1).uuid).has_value());
    auto h = membership.health(peer(1).uuid);
    assert(h.peek().state == SwimState::Suspect);
    assert(h.peek().consecutive_misses == 1);

    auto witnesses = membership.indirect_witnesses(peer(1).uuid);
    assert(witnesses.size().value() == 2);
    assert(witnesses.peers[0] == peer(2).uuid);
    assert(witnesses.peers[1] == peer(3).uuid);

    assert(membership.on_indirect_ack(peer(1).uuid, 2'000).has_value());
    h = membership.health(peer(1).uuid);
    assert(h.peek().state == SwimState::Alive);
    assert(h.peek().last_heartbeat_ns == 2'000);
    assert(h.peek().consecutive_misses == 0);

    assert(membership.on_ping_timeout(peer(2).uuid).has_value());
    assert(membership.on_indirect_timeout(peer(2).uuid).has_value());
    h = membership.health(peer(2).uuid);
    assert(h.peek().state == SwimState::Dead);

    live = membership.live_peers();
    assert(live.size() == 2);
    assert(live[0].uuid == peer(1).uuid);
    assert(live[1].uuid == peer(3).uuid);

    auto batch = membership.piggyback_batch();
    assert(batch.size().value() > 0);
    const std::uint16_t before_ack = batch.count;
    membership.acknowledge_piggybacks(1);
    assert(membership.piggyback_batch().count ==
           static_cast<std::uint16_t>(before_ack - 1u));

    std::array<SwimPeer, 1> remote_initial{admit_swim_peer(peer(1))};
    auto remote = mint_swim_membership<4>(
        crucible::effects::Init{},
        std::span<const SwimPeer>{remote_initial});
    assert(remote.size().value() == 1);

    crucible::canopy::SwimEvent gossiped_dead{};
    gossiped_dead.peer = peer(4);
    gossiped_dead.state = SwimState::Dead;
    gossiped_dead.consecutive_misses = 2;
    gossiped_dead.incarnation = 9;
    gossiped_dead.sequence = 99;
    assert(remote.apply_gossip(GossipedSwimEvent{gossiped_dead}, 3'000)
               .has_value());
    assert(remote.size().value() == 2);
    assert(remote.health(peer(4).uuid).peek().state == SwimState::Dead);

    gossiped_dead.state = SwimState::Alive;
    gossiped_dead.incarnation = 8;
    assert(remote.apply_gossip(GossipedSwimEvent{gossiped_dead}, 4'000)
               .has_value());
    assert(remote.health(peer(4).uuid).peek().state == SwimState::Dead);

    gossiped_dead.state = SwimState::Alive;
    gossiped_dead.incarnation = 10;
    assert(remote.apply_gossip(GossipedSwimEvent{gossiped_dead}, 5'000)
               .has_value());
    assert(remote.health(peer(4).uuid).peek().state == SwimState::Alive);
    assert(remote.health(peer(4).uuid).peek().last_heartbeat_ns == 5'000);

    auto missing = remote.on_ack(peer(7).uuid, 1);
    assert(!missing.has_value());
    assert(missing.error() == SwimError::PeerNotFound);

    auto tiny = mint_swim_membership<1>(crucible::effects::Init{});
    assert(tiny.add_peer(admit_swim_peer(peer(10))).has_value());
    auto overflow = tiny.add_peer(admit_swim_peer(peer(11)));
    assert(!overflow.has_value());
    assert(overflow.error() == SwimError::CapacityExceeded);

    return 0;
}
