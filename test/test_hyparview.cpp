#include <crucible/canopy/HyParView.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace {

[[nodiscard]] crucible::cog::CogIdentity peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 900};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

[[nodiscard]] crucible::canopy::HyParViewPeer
hp(std::uint64_t id) noexcept {
    auto admitted = crucible::canopy::admit_hyparview_peer(peer(id));
    assert(admitted.has_value());
    return *admitted;
}

}  // namespace

int main() {
    namespace cc = crucible::canopy;

    using Membership = cc::HyParViewMembership<3, 6>;
    static_assert(!std::is_copy_constructible_v<Membership>);
    static_assert(!std::is_move_constructible_v<Membership>);
    static_assert(std::same_as<cc::HyParViewPeer::tag_type,
                               crucible::safety::source::HyParView>);

    assert(cc::hyparview_error_name(cc::HyParViewError::PeerNotFound) ==
           std::string_view{"PeerNotFound"});
    assert(!cc::admit_hyparview_peer(crucible::cog::CogIdentity{})
                .has_value());

    std::array active{hp(1), hp(2)};
    std::array passive{hp(3), hp(4), hp(5)};
    cc::HyParViewConfig config{
        .active_size = cc::HyParViewPositiveCount{3},
        .passive_size = cc::HyParViewPositiveCount{6},
        .active_random_walk_length = cc::HyParViewPositiveCount{3},
        .passive_random_walk_length = cc::HyParViewPositiveCount{3},
        .active_random_walk_acceptance = cc::HyParViewPositiveCount{2},
        .shuffle_period_ns = cc::HyParViewDurationNs{30'000'000'000ULL},
    };
    auto membership = cc::mint_hyparview<3, 6>(
        crucible::effects::testing::init(),
        std::span<const cc::HyParViewPeer>{active},
        std::span<const cc::HyParViewPeer>{passive},
        config);

    assert(membership.active_size().value() == 2);
    assert(membership.passive_size().value() == 3);
    auto active_view = membership.active_view();
    assert(active_view.size() == 2);
    assert(active_view[0].uuid == peer(1).uuid);
    assert(active_view[1].uuid == peer(2).uuid);

    auto passive_view = membership.passive_view();
    assert(passive_view.size() == 3);
    assert(passive_view[0].uuid == peer(3).uuid);

    assert(membership.join(hp(6)).has_value());
    assert(membership.active_size().value() == 3);
    auto duplicate = membership.join(hp(6));
    assert(!duplicate.has_value());
    assert(duplicate.error() == cc::HyParViewError::DuplicatePeer);
    auto full = membership.join(hp(7));
    assert(!full.has_value());
    assert(full.error() == cc::HyParViewError::ActiveViewFull);

    cc::SwimEvent dead{};
    dead.peer = peer(2);
    dead.state = cc::SwimState::Dead;
    dead.incarnation = 2;
    assert(membership.on_swim_event(cc::GossipedSwimEvent{dead})
               .has_value());
    assert(membership.active_size().value() == 3);
    active_view = membership.active_view();
    bool saw_promoted = false;
    bool saw_dead = false;
    for (crucible::cog::CogIdentity const& id : active_view.as_span()) {
        saw_promoted = saw_promoted || id.uuid == peer(3).uuid;
        saw_dead = saw_dead || id.uuid == peer(2).uuid;
    }
    assert(saw_promoted);
    assert(!saw_dead);

    auto plan = membership.shuffle_plan();
    assert(plan.has_value());
    assert(!plan->target.uuid.is_zero());
    assert(plan->sample.count <=
           membership.config().passive_random_walk_length.value());

    cc::HyParViewShuffle<6> incoming{};
    incoming.peers[0] = peer(8);
    incoming.count = 1;
    assert(membership.apply_shuffle(
               cc::GossipedHyParViewShuffle<6>{incoming}).has_value());
    passive_view = membership.passive_view();
    bool saw_shuffle = false;
    for (crucible::cog::CogIdentity const& id : passive_view.as_span()) {
        saw_shuffle = saw_shuffle || id.uuid == peer(8).uuid;
    }
    assert(saw_shuffle);

    auto forward = membership.forward_join_plan(hp(9));
    assert(forward.has_value());
    assert(forward->joining.uuid == peer(9).uuid);
    assert(forward->ttl ==
           membership.config().active_random_walk_length.value());
    assert(forward->count ==
           membership.config().active_random_walk_acceptance.value());

    auto missing = membership.mark_failed(peer(99).uuid);
    assert(!missing.has_value());
    assert(missing.error() == cc::HyParViewError::PeerNotFound);

    return 0;
}
