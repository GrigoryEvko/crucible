#include <crucible/canopy/Crdt.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace {
struct ClockTag {};
}  // namespace

int main() {
    using crucible::canopy::CounterAmount;
    using crucible::canopy::CounterUpdate;
    using crucible::canopy::Crdt;
    using crucible::canopy::GCounter;
    using crucible::canopy::GSet;
    using crucible::canopy::HlcTimestamp;
    using crucible::canopy::LocalWrite;
    using crucible::canopy::LwwRegister;
    using crucible::canopy::LwwRegisterWrite;
    using crucible::canopy::MVRegister;
    using crucible::canopy::MVRegisterVersion;
    using crucible::canopy::OrSet;
    using crucible::canopy::OrSetAdd;
    using crucible::canopy::PNCounter;
    using crucible::canopy::ReplicaIndex;
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

    GSet<int, 8> gset_a;
    GSet<int, 8> gset_b;
    assert(gset_a.add(LocalWrite<int>{1}));
    assert(gset_a.add(LocalWrite<int>{2}));
    assert(gset_b.add(LocalWrite<int>{2}));
    assert(gset_b.add(LocalWrite<int>{3}));
    assert(gset_a.merge(gset_b));
    assert(gset_b.merge(gset_a));
    assert(gset_a.contains(1));
    assert(gset_a.contains(2));
    assert(gset_a.contains(3));
    assert(gset_a.state() == gset_b.state());

    GSet<int, 2> gset_full_a;
    GSet<int, 2> gset_full_b;
    assert(gset_full_a.add(LocalWrite<int>{1}));
    assert(gset_full_b.add(LocalWrite<int>{2}));
    assert(gset_full_b.add(LocalWrite<int>{3}));
    assert(!gset_full_a.merge(gset_full_b));
    assert(gset_full_a.size().value() == 1);
    assert(gset_full_a.contains(1));
    assert(!gset_full_a.contains(2));
    assert(!gset_full_a.contains(3));
    auto malformed_gset_state = gset_full_b.state();
    malformed_gset_state.count = 3;
    assert(!gset_full_a.merge(
        typename GSet<int, 2>::gossiped_state_type{malformed_gset_state}));
    assert(gset_full_a.size().value() == 1);
    auto gset_static_overflow =
        GSet<int, 2>::merge(gset_full_a.state(), gset_full_b.state());
    assert(gset_static_overflow.contains(1));
    assert(!gset_static_overflow.contains(2));
    assert(!gset_static_overflow.contains(3));

    OrSet<int, std::uint64_t, 8> orset_a;
    OrSet<int, std::uint64_t, 8> orset_b;
    assert(orset_a.add(LocalWrite<OrSetAdd<int, std::uint64_t>>{
        {.value = 7, .tag = 100}}));
    assert(orset_b.merge(orset_a));
    assert(orset_b.contains(7));
    assert(orset_b.remove(LocalWrite<int>{7}));
    assert(!orset_b.contains(7));
    assert(orset_a.merge(orset_b));
    assert(!orset_a.contains(7));

    OrSet<int, std::uint64_t, 2> orset_full_a;
    OrSet<int, std::uint64_t, 2> orset_full_b;
    assert(orset_full_a.add(LocalWrite<OrSetAdd<int, std::uint64_t>>{
        {.value = 1, .tag = 1}}));
    assert(orset_full_b.add(LocalWrite<OrSetAdd<int, std::uint64_t>>{
        {.value = 2, .tag = 2}}));
    assert(orset_full_b.add(LocalWrite<OrSetAdd<int, std::uint64_t>>{
        {.value = 3, .tag = 3}}));
    assert(!orset_full_a.merge(orset_full_b));
    assert(orset_full_a.contains(1));
    assert(!orset_full_a.contains(2));
    assert(!orset_full_a.contains(3));
    auto malformed_orset_state = orset_full_b.state();
    malformed_orset_state.count = 3;
    assert(!orset_full_a.merge(
        typename OrSet<int, std::uint64_t, 2>::gossiped_state_type{
            malformed_orset_state}));
    assert(orset_full_a.contains(1));
    assert(!orset_full_a.contains(2));
    assert(!orset_full_a.contains(3));
    auto orset_static_overflow =
        OrSet<int, std::uint64_t, 2>::merge(
            orset_full_a.state(),
            orset_full_b.state());
    OrSet<int, std::uint64_t, 2> orset_static_probe;
    assert(orset_static_probe.merge(
        typename OrSet<int, std::uint64_t, 2>::gossiped_state_type{
            orset_static_overflow}));
    assert(orset_static_probe.contains(1));
    assert(!orset_static_probe.contains(2));
    assert(!orset_static_probe.contains(3));

    LwwRegister<int, HlcTimestamp> lww_a;
    LwwRegister<int, HlcTimestamp> lww_b;
    assert(lww_a.assign(LocalWrite<LwwRegisterWrite<int, HlcTimestamp>>{
        {.value = 10, .clock = {.physical_ns = 5, .counter = 0}}}));
    assert(lww_b.assign(LocalWrite<LwwRegisterWrite<int, HlcTimestamp>>{
        {.value = 20, .clock = {.physical_ns = 7, .counter = 0}}}));
    assert(lww_a.merge(lww_b));
    assert(lww_a.value().has_value());
    assert(*lww_a.value() == 20);
    assert(lww_b.assign(LocalWrite<LwwRegisterWrite<int, HlcTimestamp>>{
        {.value = 30, .clock = {.physical_ns = 7, .counter = 0}}}));
    assert(lww_a.merge(lww_b));
    assert(*lww_a.value() == 30);

    GCounter<4> gc_a;
    GCounter<4> gc_b;
    assert(gc_a.increment(LocalWrite<CounterUpdate<4>>{
        {.replica = ReplicaIndex<4>{0}, .amount = CounterAmount{3}}}));
    assert(gc_b.increment(LocalWrite<CounterUpdate<4>>{
        {.replica = ReplicaIndex<4>{1}, .amount = CounterAmount{5}}}));
    assert(gc_a.merge(gc_b));
    assert(gc_b.merge(gc_a));
    assert(gc_a.value() == 8);
    assert(gc_a.state() == gc_b.state());

    PNCounter<4> pn_a;
    PNCounter<4> pn_b;
    assert(pn_a.increment(LocalWrite<CounterUpdate<4>>{
        {.replica = ReplicaIndex<4>{0}, .amount = CounterAmount{10}}}));
    assert(pn_b.decrement(LocalWrite<CounterUpdate<4>>{
        {.replica = ReplicaIndex<4>{1}, .amount = CounterAmount{4}}}));
    assert(pn_a.merge(pn_b));
    assert(pn_a.value() == 6);

    using Snap = VectorClockSnapshot<4, ClockTag>;
    MVRegister<int, 4, 4, ClockTag> mv_a;
    MVRegister<int, 4, 4, ClockTag> mv_b;
    assert(mv_a.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 1, .clock = Snap{std::in_place, 1, 0, 0, 0}}}));
    assert(mv_b.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 2, .clock = Snap{std::in_place, 0, 1, 0, 0}}}));
    assert(mv_a.merge(mv_b));
    assert(mv_a.size().value() == 2);
    assert(mv_a.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 3, .clock = Snap{std::in_place, 1, 1, 1, 0}}}));
    assert(mv_a.size().value() == 1);
    assert(mv_a.state().versions[0].value == 3);

    MVRegister<int, 2, 4, ClockTag> mv_full_a;
    MVRegister<int, 2, 4, ClockTag> mv_full_b;
    assert(mv_full_a.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 10, .clock = Snap{std::in_place, 1, 0, 0, 0}}}));
    assert(mv_full_a.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 20, .clock = Snap{std::in_place, 0, 1, 0, 0}}}));
    assert(mv_full_b.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 30, .clock = Snap{std::in_place, 0, 0, 1, 0}}}));
    assert(!mv_full_a.merge(mv_full_b));
    assert(mv_full_a.size().value() == 2);
    assert(mv_full_a.state().versions[0].value == 20);
    assert(mv_full_a.state().versions[1].value == 10);
    auto mv_static_overflow =
        MVRegister<int, 2, 4, ClockTag>::merge(
            mv_full_a.state(),
            mv_full_b.state());
    assert(mv_static_overflow.count == 2);
    assert(mv_static_overflow.versions[0].value == 20);
    assert(mv_static_overflow.versions[1].value == 10);

    MVRegister<int, 4, 4, ClockTag> mv_order_a;
    MVRegister<int, 4, 4, ClockTag> mv_order_b;
    assert(mv_order_a.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 2, .clock = Snap{std::in_place, 0, 1, 0, 0}}}));
    assert(mv_order_b.assign(LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
        {.value = 1, .clock = Snap{std::in_place, 1, 0, 0, 0}}}));
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
    assert(mv_equal_clock_a.assign(
        LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
            {.value = 40, .clock = Snap{std::in_place, 1, 1, 0, 0}}}));
    assert(mv_equal_clock_b.assign(
        LocalWrite<MVRegisterVersion<int, 4, ClockTag>>{
            {.value = 20, .clock = Snap{std::in_place, 1, 1, 0, 0}}}));
    assert(mv_equal_clock_a.merge(mv_equal_clock_b));
    assert(mv_equal_clock_b.merge(mv_equal_clock_a));
    assert(mv_equal_clock_a.state().versions[0].value == 20);
    assert(mv_equal_clock_a.state().versions[1].value == 40);
    assert(mv_equal_clock_b.state().versions[0].value == 20);
    assert(mv_equal_clock_b.state().versions[1].value == 40);

    auto malformed_mv_state = mv_full_b.state();
    malformed_mv_state.count = 3;
    assert(!mv_full_a.merge(
        typename MVRegister<int, 2, 4, ClockTag>::gossiped_state_type{
            malformed_mv_state}));
    assert(mv_full_a.size().value() == 2);
    RgaList<int, std::uint64_t, 8> rga_a;
    RgaList<int, std::uint64_t, 8> rga_b;
    assert(rga_a.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 10, .after = 0, .value = 1}}));
    assert(rga_b.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 20, .after = 10, .value = 2}}));
    assert(rga_b.merge(rga_a));
    assert(rga_a.merge(rga_b));
    auto materialized = rga_a.materialize();
    assert(materialized.count == 2);
    assert(materialized.values[0] == 1);
    assert(materialized.values[1] == 2);
    assert(rga_a.erase(LocalWrite<std::uint64_t>{10}));
    materialized = rga_a.materialize();
    assert(materialized.count == 1);
    assert(materialized.values[0] == 2);

    RgaList<int, std::uint64_t, 8> rga_conflict_a;
    RgaList<int, std::uint64_t, 8> rga_conflict_b;
    assert(rga_conflict_a.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 30, .after = 0, .value = 9}}));
    assert(rga_conflict_b.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 30, .after = 0, .value = 4}}));
    assert(rga_conflict_a.merge(rga_conflict_b));
    assert(rga_conflict_b.merge(rga_conflict_a));
    assert(rga_conflict_a.materialize().values[0] == 4);
    assert(rga_conflict_b.materialize().values[0] == 4);

    RgaList<int, std::uint64_t, 2> rga_full_a;
    RgaList<int, std::uint64_t, 2> rga_full_b;
    assert(rga_full_a.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 1, .after = 0, .value = 1}}));
    assert(rga_full_b.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 2, .after = 1, .value = 2}}));
    assert(rga_full_b.insert_after(LocalWrite<RgaInsert<std::uint64_t, int>>{
        {.id = 3, .after = 2, .value = 3}}));
    assert(!rga_full_a.merge(rga_full_b));
    auto rga_full_materialized = rga_full_a.materialize();
    assert(rga_full_materialized.count == 1);
    assert(rga_full_materialized.values[0] == 1);
    auto malformed_rga_state = rga_full_b.state();
    malformed_rga_state.count = 3;
    assert(!rga_full_a.merge(
        typename RgaList<int, std::uint64_t, 2>::gossiped_state_type{
            malformed_rga_state}));
    rga_full_materialized = rga_full_a.materialize();
    assert(rga_full_materialized.count == 1);
    assert(rga_full_materialized.values[0] == 1);
    auto rga_static_overflow =
        RgaList<int, std::uint64_t, 2>::merge(
            rga_full_a.state(),
            rga_full_b.state());
    RgaList<int, std::uint64_t, 2> rga_static_probe;
    assert(rga_static_probe.merge(
        typename RgaList<int, std::uint64_t, 2>::gossiped_state_type{
            rga_static_overflow}));
    auto rga_static_materialized = rga_static_probe.materialize();
    assert(rga_static_materialized.count == 1);
    assert(rga_static_materialized.values[0] == 1);

    return 0;
}
