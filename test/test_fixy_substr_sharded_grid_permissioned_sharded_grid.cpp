// Assertions embedded in a header are only verified under the
// project's warning flags when some translation unit includes that
// header.  This file exists to be that translation unit for the
// re-exported sharded-grid surface, and it exercises that surface at
// runtime as well.
//
// A grid has one producer slot per row and one consumer slot per
// column, and every slot has a single owner, so no pool is needed.  A
// handle knows its own slot from its type, which is why a push takes
// no slot argument.  All the slot permissions descend from one root.
//
// The default routing policy sends a producer's pushes to consumers by
// sequence number modulo the consumer count.  The affinity policy
// instead sends every push from one producer to the same consumer,
// chosen by producer index modulo the consumer count.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// The tag is local to this file so that the whole, producer and
// consumer triple is instantiated freshly here rather than reusing a
// triple some other translation unit already forced.
struct V048TestUserTag {};

struct V048TestKeyFn {
    [[nodiscard]] constexpr std::uint64_t operator()(int x) const noexcept { return static_cast<std::uint64_t>(x); }
};

}  // namespace probes

// Four producers and three consumers, so that one producer's first
// three pushes visit all three consumers in order and the routing is
// observable in a short loop.
constexpr std::size_t kM = 4;
constexpr std::size_t kN = 3;

using TestGrid = fsubstr::sharded_grid::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag>;

static_assert(
    std::is_same_v<TestGrid,
                   cc::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag, cc::RoundRobinRouting>>,
    "The re-exported grid must denote the original type.");

static_assert(std::is_same_v<fsubstr::sharded_grid::RoundRobinRouting, cc::RoundRobinRouting>);
static_assert(std::is_same_v<fsubstr::sharded_grid::HashKeyRouting<probes::V048TestKeyFn>,
                             cc::HashKeyRouting<probes::V048TestKeyFn>>);
static_assert(std::is_same_v<fsubstr::sharded_grid::AffinityRouting, cc::AffinityRouting>);

static_assert(std::is_same_v<fsubstr::sharded_grid::grid_tag::Whole<probes::V048TestUserTag>,
                             cc::grid_tag::Whole<probes::V048TestUserTag>>);
static_assert(std::is_same_v<fsubstr::sharded_grid::grid_tag::Producer<probes::V048TestUserTag, 0>,
                             cc::grid_tag::Producer<probes::V048TestUserTag, 0>>);
static_assert(std::is_same_v<fsubstr::sharded_grid::grid_tag::Producer<probes::V048TestUserTag, 3>,
                             cc::grid_tag::Producer<probes::V048TestUserTag, 3>>);
static_assert(std::is_same_v<fsubstr::sharded_grid::grid_tag::Consumer<probes::V048TestUserTag, 0>,
                             cc::grid_tag::Consumer<probes::V048TestUserTag, 0>>);
static_assert(std::is_same_v<fsubstr::sharded_grid::grid_tag::Consumer<probes::V048TestUserTag, 2>,
                             cc::grid_tag::Consumer<probes::V048TestUserTag, 2>>);

static_assert(std::is_same_v<typename TestGrid::value_type, int>);
static_assert(std::is_same_v<typename TestGrid::user_tag, probes::V048TestUserTag>);
static_assert(
    std::is_same_v<typename TestGrid::whole_tag, fsubstr::sharded_grid::grid_tag::Whole<probes::V048TestUserTag>>);

static_assert(TestGrid::num_producers == 4);
static_assert(TestGrid::num_consumers == 3);
static_assert(TestGrid::shard_capacity == 64);

static_assert(fsubstr::sharded_grid::ShardedGridSessionSurface<TestGrid>);

// The permission row for this tag tree is empty, because a handle
// transfers no permission over the wire and a token stands only for
// ownership of a slot.  That is what makes the root mint valid here
// without a context to mint against.
static void test_runtime_construct_and_grid_split() {
    TestGrid grid{};
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<TestGrid::whole_tag, kM, kN>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    // The handles are move-only, so their existence is the proof that
    // the permission tokens were consumed.
    (void)p0;
    (void)c0;
}

// Under the default policy the three pushes from one producer go to
// consumers zero, one and two in that order.
static void test_runtime_round_robin_push_pop_routing() {
    TestGrid grid{};
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<TestGrid::whole_tag, kM, kN>(std::move(whole));

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));

    if (!p0.try_push(1000)) std::abort();
    if (!p0.try_push(2000)) std::abort();
    if (!p0.try_push(3000)) std::abort();

    std::optional<int> r0 = c0.try_pop();
    if (!r0 || *r0 != 1000) std::abort();
    std::optional<int> r1 = c1.try_pop();
    if (!r1 || *r1 != 2000) std::abort();
    std::optional<int> r2 = c2.try_pop();
    if (!r2 || *r2 != 3000) std::abort();

    if (c0.try_pop()) std::abort();
    if (c1.try_pop()) std::abort();
    if (c2.try_pop()) std::abort();
}

static void test_runtime_capacity_constants() {
    static_assert(TestGrid::num_producers == kM);
    static_assert(TestGrid::num_consumers == kN);
    static_assert(TestGrid::shard_capacity == 64);
    volatile std::size_t m = TestGrid::num_producers;
    volatile std::size_t n = TestGrid::num_consumers;
    volatile std::size_t cap = TestGrid::shard_capacity;
    if (m != kM) std::abort();
    if (n != kN) std::abort();
    if (cap != 64) std::abort();
}

// The re-exported name must resolve to the original type itself, not
// to a wrapper around it.
static void test_runtime_substrate_identity() {
    static_assert(
        std::is_same_v<TestGrid,
                       cc::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag, cc::RoundRobinRouting>>);
    TestGrid grid{};
    cc::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag, cc::RoundRobinRouting>* via_sub = &grid;
    TestGrid* via_fixy = via_sub;  // implicit ptr conversion only works if types match
    if (via_fixy != via_sub) std::abort();
}

// A routing policy other than the default must reach the original
// type too.  Under affinity routing, producer one always lands on
// consumer one, because one modulo three is one.
static void test_runtime_affinity_routing_alias() {
    using AffinityGrid = fsubstr::sharded_grid::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag,
                                                                        fsubstr::sharded_grid::AffinityRouting>;
    static_assert(
        std::is_same_v<AffinityGrid,
                       cc::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag, cc::AffinityRouting>>);

    AffinityGrid grid{};
    auto whole = cs::mint_permission_root<typename AffinityGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<typename AffinityGrid::whole_tag, kM, kN>(std::move(whole));

    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));

    if (!p1.try_push(7777)) std::abort();
    std::optional<int> r = c1.try_pop();
    if (!r || *r != 7777) std::abort();
}

// The protocol aliases must denote the original types too.
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::sharded_grid::ProducerProto<int>;
    using FixyCons = fsubstr::sharded_grid::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::sharded_grid_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::sharded_grid_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

int main() {
    test_runtime_construct_and_grid_split();
    test_runtime_round_robin_push_pop_routing();
    test_runtime_capacity_constants();
    test_runtime_substrate_identity();
    test_runtime_affinity_routing_alias();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_sharded_grid_permissioned_sharded_grid: "
                "6/6 runtime witnesses passed\n");
    return 0;
}
