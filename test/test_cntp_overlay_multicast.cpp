#include <crucible/cntp/OverlayMulticast.h>

#include "test_assert.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cog::CogIdentity peer(std::uint64_t lo) noexcept {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x139, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] cntp::DeclaredOverlayPeer overlay_peer(std::uint64_t lo) {
    auto admitted = cntp::admit_overlay_peer(peer(lo));
    assert(admitted.has_value());
    return *admitted;
}

void test_admission() {
    assert(cntp::overlay_multicast_error_name(
               cntp::OverlayMulticastError::FanoutExceeded) ==
           std::string_view{"FanoutExceeded"});
    assert(cntp::admit_overlay_stripe_count(8).has_value());
    assert(!cntp::admit_overlay_stripe_count(0).has_value());
    assert(!cntp::admit_overlay_stripe_count(65).has_value());

    auto stripes = cntp::admit_overlay_stripe_count(8);
    assert(stripes.has_value());
    assert(cntp::admit_overlay_recovery_threshold(5, *stripes).has_value());
    assert(!cntp::admit_overlay_recovery_threshold(0, *stripes).has_value());
    assert(!cntp::admit_overlay_recovery_threshold(9, *stripes).has_value());
    assert(cntp::admit_overlay_fanout(2).has_value());
    assert(!cntp::admit_overlay_fanout(0).has_value());
    assert(!cntp::admit_overlay_fanout(17).has_value());

    cog::CogIdentity zero{};
    assert(!cntp::admit_overlay_peer(zero).has_value());
    static_assert(std::same_as<
                  cntp::DeclaredOverlayPeer::tag_type,
                  saf::source::OverlayMulticast>);

    std::printf("  test_admission: PASSED\n");
}

void test_routes_and_message_plan() {
    effects::ColdInitCtx init{};
    auto local = overlay_peer(1);
    std::array peers{overlay_peer(3), overlay_peer(4), overlay_peer(5)};
    auto stripes = cntp::admit_overlay_stripe_count(4);
    auto threshold = cntp::admit_overlay_recovery_threshold(3, *stripes);
    auto fanout = cntp::admit_overlay_fanout(3);
    assert(stripes.has_value());
    assert(threshold.has_value());
    assert(fanout.has_value());

    cntp::OverlayMulticastConfig cfg{
        .stripe_count = *stripes,
        .recovery_threshold = *threshold,
        .fanout = *fanout,
        .max_payload_bytes = cntp::OverlayPayloadBytes{64U},
        .use_fec_per_stripe = true,
    };
    auto plan = cntp::mint_overlay_multicast<4, 8, 4>(
        init, local, std::span<const cntp::DeclaredOverlayPeer>{peers}, cfg);
    static_assert(!std::copy_constructible<decltype(plan)>);
    assert(plan.peer_count() == 4);
    assert(plan.local_peer() == local.value());

    auto route0 = plan.route_for(0);
    auto route1 = plan.route_for(1);
    assert(route0.has_value());
    assert(route1.has_value());
    assert(route0->stripe == 0);
    assert(route1->stripe == 1);
    assert(!route0->has_parent);
    assert(route0->child_count == 3);
    assert(route0->child_count <= cfg.fanout.value());
    assert(route1->child_count <= cfg.fanout.value());
    assert(!plan.route_for(4).has_value());

    std::array<std::byte, 10> payload{};
    auto message = plan.plan_message(payload);
    assert(message.has_value());
    assert(message->stripe_count == 4);
    assert(message->stripes[0].offset == 0);
    assert(message->stripes[0].size == 3);
    assert(message->stripes[1].offset == 3);
    assert(message->stripes[1].size == 3);
    assert(message->stripes[2].offset == 6);
    assert(message->stripes[2].size == 2);
    assert(message->stripes[3].offset == 8);
    assert(message->stripes[3].size == 2);

    std::array<std::byte, 0> empty{};
    auto rejected = plan.plan_message(empty);
    assert(!rejected.has_value());
    assert(rejected.error() == cntp::OverlayMulticastError::EmptyMessage);

    std::printf("  test_routes_and_message_plan: PASSED\n");
}

void test_peer_mutation_errors() {
    effects::ColdInitCtx init{};
    auto local = overlay_peer(10);
    auto other = overlay_peer(11);
    std::array peers{other};
    auto stripes = cntp::admit_overlay_stripe_count(4);
    auto threshold = cntp::admit_overlay_recovery_threshold(3, *stripes);
    auto fanout = cntp::admit_overlay_fanout(1);
    assert(stripes.has_value());
    assert(threshold.has_value());
    assert(fanout.has_value());
    cntp::OverlayMulticastConfig cfg{
        .stripe_count = *stripes,
        .recovery_threshold = *threshold,
        .fanout = *fanout,
        .max_payload_bytes = cntp::OverlayPayloadBytes{64U},
        .use_fec_per_stripe = true,
    };
    auto plan = cntp::mint_overlay_multicast<1, 4, 1>(
        init, local, std::span<const cntp::DeclaredOverlayPeer>{peers}, cfg);

    auto duplicate = plan.add_peer(other);
    assert(!duplicate.has_value());
    assert(duplicate.error() == cntp::OverlayMulticastError::DuplicatePeer);

    auto full = plan.add_peer(overlay_peer(12));
    assert(!full.has_value());
    assert(full.error() == cntp::OverlayMulticastError::TooManyPeers);

    std::printf("  test_peer_mutation_errors: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::OverlayStripeCount) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::DeclaredOverlayPeer) ==
                  sizeof(cntp::OverlayPeerRef));
    static_assert(cntp::OverlayMulticastShape<4, 8, 2>);
    static_assert(!cntp::OverlayMulticastShape<0, 8, 2>);
    static_assert(cntp::CtxFitsOverlayMulticastMint<effects::ColdInitCtx>);
    static_assert(!cntp::CtxFitsOverlayMulticastMint<effects::BgDrainCtx>);

    std::printf("test_cntp_overlay_multicast:\n");
    test_admission();
    test_routes_and_message_plan();
    test_peer_mutation_errors();
    std::printf("test_cntp_overlay_multicast: all PASSED\n");
    return 0;
}
