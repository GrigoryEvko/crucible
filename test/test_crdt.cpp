#include <crucible/canopy/Crdt.h>

#include "test_assert.h"
#include <cstdint>
#include <type_traits>

namespace {
struct ClockTag {};
}  // namespace

int main() {
    using crucible::canopy::admit_counter_update;
    using crucible::canopy::admit_gossiped;
    using crucible::canopy::admit_local_write;
    using crucible::canopy::Crdt;
    using crucible::canopy::GCounter;
    using crucible::canopy::GSet;
    using crucible::canopy::HlcTimestamp;
    using crucible::canopy::LwwRegister;
    using crucible::canopy::LwwRegisterWrite;
    using crucible::canopy::MVRegister;
    using crucible::canopy::MVRegisterVersion;
    using crucible::canopy::OrSet;
    using crucible::canopy::OrSetAdd;
    using crucible::canopy::PNCounter;
    using crucible::canopy::RgaInsert;
    using crucible::canopy::RgaList;
    using crucible::canopy::VectorClockSnapshot;

    static_assert(Crdt<GSet<int, 8>>);
    static_assert(Crdt<OrSet<int, std::uint64_t, 8>>);
    static_assert(Crdt<LwwRegister<int, HlcTimestamp>>);
    static_assert(Crdt<GCounter<4>>);
    static_assert(Crdt<PNCounter<4>>);
    static_assert(Crdt<MVRegister<int, 4, 4, ClockTag>>);
    static_assert(Crdt<RgaList<int, std::uint64_t, 8>>);
    static_assert(!std::is_copy_constructible_v<GSet<int, 8>>);
    static_assert(!std::is_move_constructible_v<GSet<int, 8>>);

    // A local write and gossiped state come only from their doors.  A bare
    // value builds neither of the two.
    static_assert(!std::is_constructible_v<GSet<int, 8>::local_value_type, int>);
    static_assert(!std::is_constructible_v<GSet<int, 8>::gossiped_state_type, GSet<int, 8>::state_type>);

    GSet<int, 8> gset_a;
    GSet<int, 8> gset_b;
    assert(gset_a.add(admit_local_write(1)));
    assert(gset_a.add(admit_local_write(2)));
    assert(gset_b.add(admit_local_write(2)));
    assert(gset_b.add(admit_local_write(3)));
    assert(gset_a.merge(gset_b));
    assert(gset_b.merge(gset_a));
    assert(gset_a.contains(1));
    assert(gset_a.contains(2));
    assert(gset_a.contains(3));
    assert(gset_a.state() == gset_b.state());

    GSet<int, 2> gset_full_a;
    GSet<int, 2> gset_full_b;
    assert(gset_full_a.add(admit_local_write(1)));
    assert(gset_full_b.add(admit_local_write(2)));
    assert(gset_full_b.add(admit_local_write(3)));
    assert(!gset_full_a.merge(gset_full_b));
    assert(gset_full_a.size().value() == 1);
    assert(gset_full_a.contains(1));
    assert(!gset_full_a.contains(2));
    assert(!gset_full_a.contains(3));
    auto malformed_gset_state = gset_full_b.state();
    malformed_gset_state.count = 3;
    assert(!gset_full_a.merge(admit_gossiped(malformed_gset_state)));
    assert(gset_full_a.size().value() == 1);
    auto gset_static_overflow = GSet<int, 2>::merge(gset_full_a.state(), gset_full_b.state());
    assert(gset_static_overflow.contains(1));
    assert(!gset_static_overflow.contains(2));
    assert(!gset_static_overflow.contains(3));
    // A malformed target comes back unchanged from the static merge.
    auto malformed_target = GSet<int, 2>::merge(malformed_gset_state, gset_full_a.state());
    assert(malformed_target.count == 3);

    // The slot layout of a gossiped set is part of its wire format: a
    // peer receives the slot array and probes it directly.  So the hash
    // that picks the slot has to agree between every peer, and the
    // standard does not require std::hash to agree between library
    // implementations.  On this one std::hash<int> is the identity, so
    // a set built with it would place value V at slot V % Capacity and
    // a peer with a different implementation would look elsewhere.
    // Predicting the slot for a known value pins the hash in place.
    {
        constexpr auto fmix64_of = [](std::uint64_t k) constexpr {
            k ^= k >> 33;
            k *= 0xff51afd7ed558ccdULL;
            k ^= k >> 33;
            k *= 0xc4ceb9fe1a85ec53ULL;
            k ^= k >> 33;
            return k;
        };
        constexpr std::size_t expected_slot_7 = fmix64_of(7) % 8;
        static_assert(expected_slot_7 != 7, "fmix64(7) % 8 must differ from std::hash<int>(7) % 8 — "
                                            "otherwise the stable-hash check below passes whichever "
                                            "hash function is in use.  Choose a different witness "
                                            "value.");

        GSet<int, 8> stable_probe;
        assert(stable_probe.add(admit_local_write(7)));
        const auto stable_state = stable_probe.state();
        assert(stable_probe.size().value() == 1);
        assert(stable_state.slots[expected_slot_7].occupied);
        assert(stable_state.slots[expected_slot_7].value == 7);
        // Slot 7 is where the identity hash would have placed the
        // value, and it is empty.
        assert(!stable_state.slots[7].occupied);

        // Two independently built sets must agree byte for byte after
        // the same insertion, because the layout is a function of the
        // values alone and of nothing carried in the instance.
        GSet<int, 8> stable_probe_b;
        assert(stable_probe_b.add(admit_local_write(7)));
        assert(stable_probe.state() == stable_probe_b.state());
        for (std::size_t i = 0; i < 8; ++i) {
            assert(stable_state.slots[i].occupied == stable_probe_b.state().slots[i].occupied);
            if (stable_state.slots[i].occupied) {
                assert(stable_state.slots[i].value == stable_probe_b.state().slots[i].value);
            }
        }

        // The receiver takes a peer's state whole, with no insertion of
        // its own to rebuild the layout.  Its probe therefore has to
        // walk the slots the producer chose, which it can only do if
        // both sides hash alike.
        GSet<int, 8> gossip_receiver;
        assert(gossip_receiver.merge(admit_gossiped(stable_state)));
        assert(gossip_receiver.contains(7));
        assert(!gossip_receiver.contains(8));
        assert(gossip_receiver.state() == stable_state);
    }

    using OrAdd = OrSetAdd<int, std::uint64_t>;
    OrSet<int, std::uint64_t, 8> orset_a;
    OrSet<int, std::uint64_t, 8> orset_b;
    assert(orset_a.add(admit_local_write(OrAdd{.value = 7, .tag = 100})));
    assert(orset_b.merge(orset_a));
    assert(orset_b.contains(7));
    assert(orset_b.remove(admit_local_write(7)));
    assert(!orset_b.contains(7));
    assert(orset_a.merge(orset_b));
    assert(!orset_a.contains(7));

    OrSet<int, std::uint64_t, 2> orset_full_a;
    OrSet<int, std::uint64_t, 2> orset_full_b;
    assert(orset_full_a.add(admit_local_write(OrAdd{.value = 1, .tag = 1})));
    assert(orset_full_b.add(admit_local_write(OrAdd{.value = 2, .tag = 2})));
    assert(orset_full_b.add(admit_local_write(OrAdd{.value = 3, .tag = 3})));
    assert(!orset_full_a.merge(orset_full_b));
    assert(orset_full_a.contains(1));
    assert(!orset_full_a.contains(2));
    assert(!orset_full_a.contains(3));
    auto malformed_orset_state = orset_full_b.state();
    malformed_orset_state.count = 3;
    assert(!orset_full_a.merge(admit_gossiped(malformed_orset_state)));
    assert(orset_full_a.contains(1));
    assert(!orset_full_a.contains(2));
    assert(!orset_full_a.contains(3));
    auto orset_static_overflow = OrSet<int, std::uint64_t, 2>::merge(orset_full_a.state(), orset_full_b.state());
    OrSet<int, std::uint64_t, 2> orset_static_probe;
    assert(orset_static_probe.merge(admit_gossiped(orset_static_overflow)));
    assert(orset_static_probe.contains(1));
    assert(!orset_static_probe.contains(2));
    assert(!orset_static_probe.contains(3));

    using LwwWrite = LwwRegisterWrite<int, HlcTimestamp>;
    LwwRegister<int, HlcTimestamp> lww_a;
    LwwRegister<int, HlcTimestamp> lww_b;
    assert(lww_a.assign(admit_local_write(LwwWrite{.value = 10, .clock = {.physical_ns = 5, .counter = 0}})));
    assert(lww_b.assign(admit_local_write(LwwWrite{.value = 20, .clock = {.physical_ns = 7, .counter = 0}})));
    assert(lww_a.merge(lww_b));
    assert(lww_a.value().has_value());
    assert(*lww_a.value() == 20);
    assert(lww_b.assign(admit_local_write(LwwWrite{.value = 30, .clock = {.physical_ns = 7, .counter = 0}})));
    assert(lww_a.merge(lww_b));
    assert(*lww_a.value() == 30);

    // The counter door refuses a replica outside the counter and an
    // amount of zero, and admits the rest.
    assert(!admit_counter_update<4>(4, 1).has_value());
    assert(!admit_counter_update<4>(0, 0).has_value());
    assert(admit_counter_update<4>(3, 1).has_value());

    GCounter<4> gc_a;
    GCounter<4> gc_b;
    assert(gc_a.increment(*admit_counter_update<4>(0, 3)));
    assert(gc_b.increment(*admit_counter_update<4>(1, 5)));
    assert(gc_a.merge(gc_b));
    assert(gc_b.merge(gc_a));
    assert(gc_a.value() == 8);
    assert(gc_a.state() == gc_b.state());

    // The count of one replica saturates at the maximum and does not wrap.
    GCounter<2> gc_saturate;
    assert(gc_saturate.increment(*admit_counter_update<2>(0, UINT64_MAX)));
    assert(gc_saturate.increment(*admit_counter_update<2>(0, 1)));
    assert(gc_saturate.increment(*admit_counter_update<2>(1, 1)));
    assert(gc_saturate.value() == UINT64_MAX);

    PNCounter<4> pn_a;
    PNCounter<4> pn_b;
    assert(pn_a.increment(*admit_counter_update<4>(0, 10)));
    assert(pn_b.decrement(*admit_counter_update<4>(1, 4)));
    assert(pn_a.merge(pn_b));
    assert(pn_a.value() == 6);

    using Snap = VectorClockSnapshot<4, ClockTag>;
    using MvVersion = MVRegisterVersion<int, 4, ClockTag>;
    MVRegister<int, 4, 4, ClockTag> mv_a;
    MVRegister<int, 4, 4, ClockTag> mv_b;
    assert(mv_a.assign(admit_local_write(MvVersion{.value = 1, .clock = Snap{std::in_place, 1, 0, 0, 0}})));
    assert(mv_b.assign(admit_local_write(MvVersion{.value = 2, .clock = Snap{std::in_place, 0, 1, 0, 0}})));
    assert(mv_a.merge(mv_b));
    assert(mv_a.size().value() == 2);
    assert(mv_a.assign(admit_local_write(MvVersion{.value = 3, .clock = Snap{std::in_place, 1, 1, 1, 0}})));
    assert(mv_a.size().value() == 1);
    assert(mv_a.state().versions[0].value == 3);

    MVRegister<int, 2, 4, ClockTag> mv_full_a;
    MVRegister<int, 2, 4, ClockTag> mv_full_b;
    assert(mv_full_a.assign(admit_local_write(MvVersion{.value = 10, .clock = Snap{std::in_place, 1, 0, 0, 0}})));
    assert(mv_full_a.assign(admit_local_write(MvVersion{.value = 20, .clock = Snap{std::in_place, 0, 1, 0, 0}})));
    assert(mv_full_b.assign(admit_local_write(MvVersion{.value = 30, .clock = Snap{std::in_place, 0, 0, 1, 0}})));
    assert(!mv_full_a.merge(mv_full_b));
    assert(mv_full_a.size().value() == 2);
    assert(mv_full_a.state().versions[0].value == 20);
    assert(mv_full_a.state().versions[1].value == 10);
    auto mv_static_overflow = MVRegister<int, 2, 4, ClockTag>::merge(mv_full_a.state(), mv_full_b.state());
    assert(mv_static_overflow.count == 2);
    assert(mv_static_overflow.versions[0].value == 20);
    assert(mv_static_overflow.versions[1].value == 10);

    MVRegister<int, 4, 4, ClockTag> mv_order_a;
    MVRegister<int, 4, 4, ClockTag> mv_order_b;
    assert(mv_order_a.assign(admit_local_write(MvVersion{.value = 2, .clock = Snap{std::in_place, 0, 1, 0, 0}})));
    assert(mv_order_b.assign(admit_local_write(MvVersion{.value = 1, .clock = Snap{std::in_place, 1, 0, 0, 0}})));
    assert(mv_order_a.merge(mv_order_b));
    assert(mv_order_b.merge(mv_order_a));
    assert(mv_order_a.state().count == 2);
    assert(mv_order_b.state().count == 2);
    assert(mv_order_a.state().versions[0].value == 2);
    assert(mv_order_a.state().versions[1].value == 1);
    assert(mv_order_b.state().versions[0].value == 2);
    assert(mv_order_b.state().versions[1].value == 1);

    MVRegister<int, 4, 4, ClockTag> mv_equal_clock_a;
    MVRegister<int, 4, 4, ClockTag> mv_equal_clock_b;
    assert(
        mv_equal_clock_a.assign(admit_local_write(MvVersion{.value = 40, .clock = Snap{std::in_place, 1, 1, 0, 0}})));
    assert(
        mv_equal_clock_b.assign(admit_local_write(MvVersion{.value = 20, .clock = Snap{std::in_place, 1, 1, 0, 0}})));
    assert(mv_equal_clock_a.merge(mv_equal_clock_b));
    assert(mv_equal_clock_b.merge(mv_equal_clock_a));
    assert(mv_equal_clock_a.state().versions[0].value == 20);
    assert(mv_equal_clock_a.state().versions[1].value == 40);
    assert(mv_equal_clock_b.state().versions[0].value == 20);
    assert(mv_equal_clock_b.state().versions[1].value == 40);

    auto malformed_mv_state = mv_full_b.state();
    malformed_mv_state.count = 3;
    assert(!mv_full_a.merge(admit_gossiped(malformed_mv_state)));
    assert(mv_full_a.size().value() == 2);
    // An empty gossiped state merges nothing, so the only guard against
    // a malformed target is the one on the target itself.
    auto mv_malformed_target = MVRegister<int, 2, 4, ClockTag>::merge(malformed_mv_state, {});
    assert(mv_malformed_target.count == 3);

    using Insert = RgaInsert<std::uint64_t, int>;
    RgaList<int, std::uint64_t, 8> rga_a;
    RgaList<int, std::uint64_t, 8> rga_b;
    assert(rga_a.insert_after(admit_local_write(Insert{.id = 10, .after = 0, .value = 1})));
    assert(rga_b.insert_after(admit_local_write(Insert{.id = 20, .after = 10, .value = 2})));
    assert(rga_b.merge(rga_a));
    assert(rga_a.merge(rga_b));
    auto materialized = rga_a.materialize();
    assert(materialized.count == 2);
    assert(materialized.values[0] == 1);
    assert(materialized.values[1] == 2);
    assert(rga_a.erase(admit_local_write(std::uint64_t{10})));
    materialized = rga_a.materialize();
    assert(materialized.count == 1);
    assert(materialized.values[0] == 2);

    RgaList<int, std::uint64_t, 8> rga_conflict_a;
    RgaList<int, std::uint64_t, 8> rga_conflict_b;
    assert(rga_conflict_a.insert_after(admit_local_write(Insert{.id = 30, .after = 0, .value = 9})));
    assert(rga_conflict_b.insert_after(admit_local_write(Insert{.id = 30, .after = 0, .value = 4})));
    assert(rga_conflict_a.merge(rga_conflict_b));
    assert(rga_conflict_b.merge(rga_conflict_a));
    assert(rga_conflict_a.materialize().values[0] == 4);
    assert(rga_conflict_b.materialize().values[0] == 4);

    RgaList<int, std::uint64_t, 2> rga_full_a;
    RgaList<int, std::uint64_t, 2> rga_full_b;
    assert(rga_full_a.insert_after(admit_local_write(Insert{.id = 1, .after = 0, .value = 1})));
    assert(rga_full_b.insert_after(admit_local_write(Insert{.id = 2, .after = 1, .value = 2})));
    assert(rga_full_b.insert_after(admit_local_write(Insert{.id = 3, .after = 2, .value = 3})));
    assert(!rga_full_a.merge(rga_full_b));
    auto rga_full_materialized = rga_full_a.materialize();
    assert(rga_full_materialized.count == 1);
    assert(rga_full_materialized.values[0] == 1);
    auto malformed_rga_state = rga_full_b.state();
    malformed_rga_state.count = 3;
    assert(!rga_full_a.merge(admit_gossiped(malformed_rga_state)));
    rga_full_materialized = rga_full_a.materialize();
    assert(rga_full_materialized.count == 1);
    assert(rga_full_materialized.values[0] == 1);
    auto rga_static_overflow = RgaList<int, std::uint64_t, 2>::merge(rga_full_a.state(), rga_full_b.state());
    RgaList<int, std::uint64_t, 2> rga_static_probe;
    assert(rga_static_probe.merge(admit_gossiped(rga_static_overflow)));
    auto rga_static_materialized = rga_static_probe.materialize();
    assert(rga_static_materialized.count == 1);
    assert(rga_static_materialized.values[0] == 1);

    return 0;
}
