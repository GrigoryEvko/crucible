#include <crucible/canopy/HyParView.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
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
    // FIXY-U-107: promotion is Philox-derived (was passive_[0]).
    // Invariant: some original passive peer (3/4/5) ends up in active.
    for (crucible::cog::CogIdentity const& id : active_view.as_span()) {
        saw_promoted = saw_promoted ||
                       id.uuid == peer(3).uuid ||
                       id.uuid == peer(4).uuid ||
                       id.uuid == peer(5).uuid;
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

    // fixy-A5-030 regression: recoverable admission path must reject
    // over-capacity config and over-capacity peer spans WITHOUT
    // aborting the process via CRUCIBLE_FATAL_INVARIANT.  Pre-fix the
    // only way to construct from untrusted input was the trapping
    // constructor; now callers can pre-validate and recover.

    // Config too large for template params: active_size > MaxActive.
    {
        cc::HyParViewConfig too_big{
            .active_size = cc::HyParViewPositiveCount{99},
            .passive_size = cc::HyParViewPositiveCount{200},
        };
        auto admitted = cc::admit_hyparview_config<3, 6>(too_big);
        assert(!admitted.has_value());
        assert(admitted.error() == cc::HyParViewError::InvalidConfig);
    }

    // Config valid → returns the config unchanged.
    {
        cc::HyParViewConfig ok{
            .active_size = cc::HyParViewPositiveCount{2},
            .passive_size = cc::HyParViewPositiveCount{4},
        };
        auto admitted = cc::admit_hyparview_config<3, 6>(ok);
        assert(admitted.has_value());
        assert(admitted->active_size.value() == 2);
    }

    // Active span larger than config.active_size → ActiveViewFull
    // (would have trapped pre-fix via the peer-list constructor).
    {
        cc::HyParViewConfig small_cfg{
            .active_size = cc::HyParViewPositiveCount{1},
            .passive_size = cc::HyParViewPositiveCount{4},
        };
        auto admitted = cc::admit_hyparview_config<3, 6>(small_cfg);
        assert(admitted.has_value());
        cc::HyParViewMembership<3, 6> m{*admitted};
        std::array over_capacity{hp(50), hp(51), hp(52)};  // 3 > 1
        auto rc = cc::populate_hyparview_membership(
            m,
            std::span<const cc::HyParViewPeer>{over_capacity});
        assert(!rc.has_value());
        assert(rc.error() == cc::HyParViewError::ActiveViewFull);
        // Membership stays empty — early reject avoided partial fill.
        assert(m.active_size().value() == 0);
    }

    // Happy path: well-sized span + valid peers → succeeds.
    {
        cc::HyParViewConfig cfg{
            .active_size = cc::HyParViewPositiveCount{2},
            .passive_size = cc::HyParViewPositiveCount{4},
        };
        auto admitted = cc::admit_hyparview_config<3, 6>(cfg);
        assert(admitted.has_value());
        cc::HyParViewMembership<3, 6> m{*admitted};
        std::array good_active{hp(60), hp(61)};
        std::array good_passive{hp(70), hp(71), hp(72)};
        auto rc = cc::populate_hyparview_membership(
            m,
            std::span<const cc::HyParViewPeer>{good_active},
            std::span<const cc::HyParViewPeer>{good_passive});
        assert(rc.has_value());
        assert(m.active_size().value() == 2);
        assert(m.passive_size().value() == 3);
    }

    return 0;
}
