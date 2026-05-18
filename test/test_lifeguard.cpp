#include <crucible/canopy/Lifeguard.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

[[nodiscard]] crucible::cog::CogIdentity
peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 2100};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

[[nodiscard]] crucible::canopy::SwimPeer
swim_peer(std::uint64_t id) noexcept {
    return crucible::canopy::admit_swim_peer(peer(id));
}

}  // namespace

int main() {
    namespace cc = crucible::canopy;

    using Lifeguard = cc::LifeguardSwim<4, 8, 4, 8>;
    static_assert(!std::is_copy_constructible_v<Lifeguard>);
    static_assert(!std::is_move_constructible_v<Lifeguard>);
    static_assert(alignof(Lifeguard) >= 64);

    assert(cc::lifeguard_error_name(cc::LifeguardError::PeerNotFound) ==
           std::string_view{"PeerNotFound"});

    std::array initial{swim_peer(1), swim_peer(2), swim_peer(3)};
    cc::LifeguardConfig lcfg{
        .base_probe_timeout_ns = cc::LifeguardDurationNs{100'000'000ULL},
        .min_indirect_checks = cc::LifeguardPositiveCount{1},
        .max_indirect_checks = cc::LifeguardPositiveCount{3},
        .min_lhm = cc::LifeguardMultiplier{1},
        .max_lhm = cc::LifeguardMultiplier{4},
        .lhm_timeout_penalty = cc::LifeguardPositiveCount{1},
        .lhm_success_recovery = cc::LifeguardPositiveCount{1},
        .rtt_safety_multiplier = cc::LifeguardPositiveCount{3},
    };
    cc::SwimConfig scfg{
        .period_ns = cc::SwimDurationNs{1'000'000'000ULL},
        .ack_timeout_ns = cc::SwimDurationNs{50'000'000ULL},
        .indirect_timeout_ns = cc::SwimDurationNs{50'000'000ULL},
        .indirect_checks = cc::SwimPositiveCount{2},
        .suspicion_misses = cc::SwimPositiveCount{3},
    };

    auto lifeguard = cc::mint_lifeguard_swim<4, 8, 4, 8>(
        crucible::effects::testing::init(),
        swim_peer(99),
        std::span<const cc::SwimPeer>{initial},
        lcfg,
        scfg);

    assert(lifeguard.size().value() == 3);
    assert(lifeguard.local_peer().uuid == peer(99).uuid);
    assert(lifeguard.swim_config().suspicion_misses.value() == 3);
    assert(lifeguard.local_health_multiplier(peer(1).uuid)->value() == 1);

    auto first = lifeguard.next_probe(1'000);
    assert(first.has_value());
    assert(first->target == peer(1).uuid);
    assert(first->timeout_ns == 100'000'000ULL);
    assert(first->deadline_ns == 100'001'000ULL);
    assert(first->indirect_checks == 1);

    assert(lifeguard.on_ping_timeout(peer(1).uuid).has_value());
    assert(lifeguard.local_health_multiplier(peer(1).uuid)->value() == 2);
    auto timeout = lifeguard.adaptive_timeout(peer(1).uuid);
    assert(timeout.has_value());
    assert(timeout->value() == 200'000'000ULL);
    auto witnesses = lifeguard.indirect_witnesses(peer(1).uuid);
    assert(witnesses.size().value() == 2);
    assert(witnesses.peers[0] == peer(2).uuid);
    assert(witnesses.peers[1] == peer(3).uuid);

    assert(lifeguard.on_ping_timeout(peer(1).uuid).has_value());
    assert(lifeguard.on_ping_timeout(peer(1).uuid).has_value());
    assert(lifeguard.local_health_multiplier(peer(1).uuid)->value() == 4);
    assert(lifeguard.adaptive_timeout(peer(1).uuid)->value() ==
           400'000'000ULL);
    assert(lifeguard.swim().health(peer(1).uuid).peek().state ==
           cc::SwimState::Dead);

    assert(lifeguard.on_indirect_ack(
        peer(1).uuid,
        9'000,
        cc::LifeguardRttNs{40'000'000ULL}).has_value());
    assert(lifeguard.swim().health(peer(1).uuid).peek().state ==
           cc::SwimState::Alive);
    assert(lifeguard.local_health_multiplier(peer(1).uuid)->value() == 3);
    assert(lifeguard.adaptive_timeout(peer(1).uuid)->value() ==
           300'000'000ULL);

    assert(lifeguard.on_ack(
        peer(1).uuid,
        10'000,
        cc::LifeguardRttNs{150'000'000ULL}).has_value());
    assert(lifeguard.local_health_multiplier(peer(1).uuid)->value() == 2);
    assert(lifeguard.adaptive_timeout(peer(1).uuid)->value() ==
           285'000'000ULL);

    auto events = lifeguard.event_batch();
    assert(events.size().value() >= 5);
    assert(events.events[0].outcome == cc::LifeguardOutcome::Timeout);
    assert(events.events[0].prior_lhm == 1);
    assert(events.events[0].next_lhm == 2);
    lifeguard.acknowledge_events(2);
    assert(lifeguard.event_batch().size().value() ==
           static_cast<std::uint16_t>(events.size().value() - 2u));

    cc::SwimEvent suspect_self{};
    suspect_self.peer = peer(99);
    suspect_self.state = cc::SwimState::Suspect;
    suspect_self.incarnation = 7;
    suspect_self.sequence = 77;
    auto refute = lifeguard.apply_gossip(
        cc::GossipedSwimEvent{suspect_self},
        11'000);
    assert(refute.has_value());
    assert(refute->should_refute);
    assert(refute->alive.peer.uuid == peer(99).uuid);
    assert(refute->alive.state == cc::SwimState::Alive);
    assert(refute->alive.incarnation == 8);

    cc::SwimEvent remote_suspect{};
    remote_suspect.peer = peer(4);
    remote_suspect.state = cc::SwimState::Suspect;
    remote_suspect.consecutive_misses = 1;
    remote_suspect.incarnation = 3;
    remote_suspect.sequence = 80;
    auto applied = lifeguard.apply_gossip(
        cc::GossipedSwimEvent{remote_suspect},
        12'000);
    assert(applied.has_value());
    assert(!applied->should_refute);
    assert(lifeguard.size().value() == 4);

    auto duplicate = lifeguard.add_peer(swim_peer(2));
    assert(!duplicate.has_value());
    assert(duplicate.error() == cc::LifeguardError::DuplicatePeer);

    auto missing = lifeguard.on_no_loss_window(peer(44).uuid);
    assert(!missing.has_value());
    assert(missing.error() == cc::LifeguardError::PeerNotFound);

    return 0;
}
