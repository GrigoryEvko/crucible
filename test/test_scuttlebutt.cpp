#include <crucible/canopy/Scuttlebutt.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace {

[[nodiscard]] crucible::cog::CogIdentity
peer(std::uint64_t id) noexcept {
    crucible::cog::CogIdentity out{};
    out.uuid = crucible::cog::Uuid{id, id + 10};
    out.kind = crucible::cog::CogKind::NicPort;
    return out;
}

}  // namespace

int main() {
    namespace cc = crucible::canopy;

    using Set = cc::GSet<std::uint64_t, 8>;
    using Reg = cc::LwwRegister<std::uint64_t, cc::HlcTimestamp>;
    using Sync = cc::ScuttlebuttSync<4, 4>;

    static_assert(!std::is_copy_constructible_v<Sync>);
    static_assert(!std::is_move_constructible_v<Sync>);
    static_assert(alignof(Sync) >= 64);

    const auto p1 = cc::admit_swim_peer(peer(1));
    const auto p2 = cc::admit_swim_peer(peer(2));
    const auto p3 = cc::admit_swim_peer(peer(3));

    std::array<cc::SwimPeer, 1> a_peers{p2};
    std::array<cc::SwimPeer, 1> b_peers{p1};

    auto a = cc::mint_scuttlebutt<4, 4>(
        crucible::effects::Init{},
        p1,
        std::span<const cc::SwimPeer>{a_peers});
    auto b = cc::mint_scuttlebutt<4, 4>(
        crucible::effects::Init{},
        p2,
        std::span<const cc::SwimPeer>{b_peers});

    assert(a.peer_count() == 2);
    assert(a.key_count() == 0);
    assert(a.config().period_ns.value() == 5'000'000'000ULL);

    auto raw_key = cc::admit_scuttlebutt_key("topology/nodes");
    assert(raw_key.has_value());
    auto empty_key = cc::admit_scuttlebutt_key("");
    assert(!empty_key.has_value());
    assert(empty_key.error() == cc::ScuttlebuttError::EmptyKey);

    const cc::LocalScuttlebuttKey key = raw_key.value();
    Set set_a{};
    Set set_b{};
    assert(a.register_state(key, set_a).has_value());
    assert(b.register_state(key, set_b).has_value());
    assert(a.key_count() == 1);

    assert(set_a.add(cc::LocalWrite<std::uint64_t>{42}));
    auto published = a.publish_local_change(key, set_a);
    assert(published.has_value());
    assert(a.publish_count() == 1);

    auto digest_a = a.digest();
    assert(digest_a.size().value() == 1);
    auto diff_b = b.compare_digest(
        cc::GossipedScuttlebuttDigest<4, 4>{digest_a});
    assert(diff_b.has_value());
    assert(diff_b->requests.size().value() == 1);
    assert(diff_b->offers.size().value() == 0);

    auto offered = a.delta_for_request(
        diff_b->requests.entries[0],
        set_a);
    assert(offered.has_value());

    auto changed = b.apply_delta(
        cc::GossipedScuttlebuttDelta<Set::state_type>{
            offered.value().value()},
        set_b);
    assert(changed.has_value());
    assert(changed.value());
    assert(set_b.contains(42));
    assert(b.merge_count() == 1);

    auto repeated = b.apply_delta(
        cc::GossipedScuttlebuttDelta<Set::state_type>{
            offered.value().value()},
        set_b);
    assert(repeated.has_value());
    assert(!repeated.value());

    auto settled = a.compare_digest(
        cc::GossipedScuttlebuttDigest<4, 4>{b.digest()});
    assert(settled.has_value());
    assert(settled->requests.size().value() == 0);
    assert(settled->offers.size().value() == 0);

    Reg reg_a{};
    Reg reg_b{};
    auto reg_key_result = cc::admit_scuttlebutt_key("edge/rtt/p99");
    assert(reg_key_result.has_value());
    const cc::LocalScuttlebuttKey reg_key = reg_key_result.value();
    assert(a.register_state(reg_key, reg_a).has_value());
    assert(b.register_state(reg_key, reg_b).has_value());
    auto wrong_type = a.register_state(key, reg_a);
    assert(!wrong_type.has_value());
    assert(wrong_type.error() == cc::ScuttlebuttError::TypeMismatch);

    assert(reg_a.assign(cc::LocalWrite<Reg::write_type>{
        Reg::write_type{
            .value = 99,
            .clock = cc::HlcTimestamp{.physical_ns = 1000, .counter = 1},
        }}));
    auto reg_delta = a.publish_local_change(reg_key, reg_a);
    assert(reg_delta.has_value());

    auto reg_diff = b.compare_digest(
        cc::GossipedScuttlebuttDigest<4, 4>{a.digest()});
    assert(reg_diff.has_value());
    assert(reg_diff->requests.size().value() == 1);
    auto reg_offer = a.delta_for_request(
        reg_diff->requests.entries[0],
        reg_a);
    assert(reg_offer.has_value());
    auto reg_changed = b.apply_delta(
        cc::GossipedScuttlebuttDelta<Reg::state_type>{
            reg_offer.value().value()},
        reg_b);
    assert(reg_changed.has_value());
    assert(reg_changed.value());
    assert(reg_b.value().has_value());
    assert(reg_b.value().value() == 99);

    cc::ScuttlebuttDigest<4, 4> malformed{};
    assert(malformed.push(cc::ScuttlebuttVersionEntry{
        .origin = peer(1).uuid,
        .key = key.value(),
        .version = 1,
    }));
    malformed.entries[malformed.count] = malformed.entries[0];
    ++malformed.count;
    auto malformed_diff = b.compare_digest(
        cc::GossipedScuttlebuttDigest<4, 4>{malformed});
    assert(!malformed_diff.has_value());
    assert(malformed_diff.error() == cc::ScuttlebuttError::MalformedDigest);

    auto missing_peer = b.add_peer(p3);
    assert(missing_peer.has_value());
    auto duplicate_peer = b.add_peer(p3);
    assert(!duplicate_peer.has_value());
    assert(duplicate_peer.error() == cc::ScuttlebuttError::DuplicatePeer);

    auto compacted = b.compact_peer_versions(p1, 2);
    assert(compacted.has_value());
    assert(compacted.value() == 2);

    auto tiny = cc::mint_scuttlebutt<2, 1>(crucible::effects::Init{}, p1);
    assert(tiny.add_peer(p2).has_value());
    auto overflow_peer = tiny.add_peer(p3);
    assert(!overflow_peer.has_value());
    assert(overflow_peer.error() == cc::ScuttlebuttError::CapacityExceeded);

    Set tiny_set{};
    auto key2_result = cc::admit_scuttlebutt_key("k2");
    assert(key2_result.has_value());
    assert(tiny.register_state(key, tiny_set).has_value());
    auto overflow_key = tiny.register_state(key2_result.value(), tiny_set);
    assert(!overflow_key.has_value());
    assert(overflow_key.error() == cc::ScuttlebuttError::CapacityExceeded);

    return 0;
}
