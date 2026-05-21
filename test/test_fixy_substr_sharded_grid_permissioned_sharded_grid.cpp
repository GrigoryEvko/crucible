// ── test_fixy_substr_sharded_grid_permissioned_sharded_grid — V-048 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v048:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-048 ShardedGrid substrate-direct surface additions:
//   * PermissionedShardedGrid<T, M, N, Cap, UserTag, Routing>  — substrate alias
//   * RoundRobinRouting / HashKeyRouting / AffinityRouting     — routing policy re-exports
//   * grid_tag::{Whole, Producer<I>, Consumer<J>}<UserTag>     — tag tree (FOUND-A22 Slice-indexed)
//   (ShardedGridSessionSurface already shipped pre-V-048 — but here is admitted)
//   (mint_sharded_grid_* + mint_*_session already shipped pre-V-048 via using-decl)
//
// ShardedGrid structural notes (vs V-045 SPSC / V-046 MPMC / V-047 ChaseLev):
//   * Fifth cell of the channel-permission family — linear-grid ×
//     linear-grid (M producer slots × N consumer slots).  Each slot
//     is single-owner; no Pool needed.
//   * Statically-indexed handles — ProducerHandle<I> knows its shard
//     I at the type level; try_push takes no id parameter.
//   * Permissions descend from a single Whole<UserTag> root via the
//     FOUND-A22 mint_grid_permissions<Whole, M, N>(parent) factory
//     (surfaced at fixy::perm::mint_grid_permissions).
//   * Default Routing = RoundRobinRouting — consumer = seq % N per
//     producer, so producer 0's first N pushes round-robin across
//     consumers 0..N-1 in order.

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
namespace cc      = ::crucible::concurrent;
namespace cs      = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Synthetic UserTag + KeyFn for V-048 fixtures ──────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedShardedGrid.h's generic specialization
// picks up the fresh whole_tag / producer-side / consumer-side
// triple via the UserTag-parameterized templates.
struct V048TestUserTag {};

// Stateless callable for HashKeyRouting<KeyFn> identity witness.
struct V048TestKeyFn {
    [[nodiscard]] constexpr std::uint64_t operator()(int x) const noexcept {
        return static_cast<std::uint64_t>(x);
    }
};

}  // namespace probes

// 4 producers × 3 consumers × capacity 64 — round-numbers chosen so
// RoundRobinRouting's "first N pushes from producer 0 visit consumers
// 0..N-1 in order" property is observable with a tight loop.
constexpr std::size_t kM = 4;
constexpr std::size_t kN = 3;

using TestGrid =
    fsubstr::sharded_grid::PermissionedShardedGrid<int, kM, kN, 64,
                                                    probes::V048TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity (default RoundRobinRouting) ─────
static_assert(std::is_same_v<
    TestGrid,
    cc::PermissionedShardedGrid<int, kM, kN, 64, probes::V048TestUserTag,
                                cc::RoundRobinRouting>>,
    "fixy::substr::sharded_grid::PermissionedShardedGrid must alias the substrate.");

// ── 2. Routing policy re-exports — alias identity ───────────────
static_assert(std::is_same_v<
    fsubstr::sharded_grid::RoundRobinRouting,
    cc::RoundRobinRouting>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::HashKeyRouting<probes::V048TestKeyFn>,
    cc::HashKeyRouting<probes::V048TestKeyFn>>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::AffinityRouting,
    cc::AffinityRouting>);

// ── 3. Tag tree identity — Whole / Producer<I> / Consumer<J> ─────
static_assert(std::is_same_v<
    fsubstr::sharded_grid::grid_tag::Whole<probes::V048TestUserTag>,
    cc::grid_tag::Whole<probes::V048TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::grid_tag::Producer<probes::V048TestUserTag, 0>,
    cc::grid_tag::Producer<probes::V048TestUserTag, 0>>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::grid_tag::Producer<probes::V048TestUserTag, 3>,
    cc::grid_tag::Producer<probes::V048TestUserTag, 3>>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::grid_tag::Consumer<probes::V048TestUserTag, 0>,
    cc::grid_tag::Consumer<probes::V048TestUserTag, 0>>);
static_assert(std::is_same_v<
    fsubstr::sharded_grid::grid_tag::Consumer<probes::V048TestUserTag, 2>,
    cc::grid_tag::Consumer<probes::V048TestUserTag, 2>>);

// ── 4. Member typedef parity through TestGrid ────────────────────
static_assert(std::is_same_v<typename TestGrid::value_type, int>);
static_assert(std::is_same_v<typename TestGrid::user_tag,
                             probes::V048TestUserTag>);
static_assert(std::is_same_v<typename TestGrid::whole_tag,
                             fsubstr::sharded_grid::grid_tag::Whole<probes::V048TestUserTag>>);

// ── 5. Value-template parity ────────────────────────────────────
static_assert(TestGrid::num_producers == 4);
static_assert(TestGrid::num_consumers == 3);
static_assert(TestGrid::shard_capacity == 64);

// ── 6. ShardedGridSessionSurface admits the representative grid ──
static_assert(fsubstr::sharded_grid::ShardedGridSessionSurface<TestGrid>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct + mint whole permission + split via FOUND-A22 grid factory.
// mint_grid_permissions (no-ctx form) is valid here because the grid
// tag tree's permission_row<Tag> is empty (handles are EmptyPermSet —
// no wire-permission transfer; permission tokens proxy the linear
// slot ownership only).
static void test_runtime_construct_and_grid_split() {
    TestGrid grid{};
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<
        TestGrid::whole_tag, kM, kN>(std::move(whole));
    // Construct one producer + one consumer to prove the factory wiring.
    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(
        std::move(std::get<0>(perms.consumers)));
    // Both handles are move-only; their existence proves the linear
    // Permission tokens were consumed.
    (void)p0;
    (void)c0;
}

// Owner pushes from producer<0>: RoundRobinRouting sends seq 0,1,2 to
// consumers 0,1,2 respectively.  Each consumer pops its routed item.
static void test_runtime_round_robin_push_pop_routing() {
    TestGrid grid{};
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<
        TestGrid::whole_tag, kM, kN>(std::move(whole));

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(
        std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(
        std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(
        std::move(std::get<2>(perms.consumers)));

    // Push three items from producer 0 — RoundRobinRouting sends
    // seq=0 → consumer 0, seq=1 → consumer 1, seq=2 → consumer 2.
    if (!p0.try_push(1000)) std::abort();
    if (!p0.try_push(2000)) std::abort();
    if (!p0.try_push(3000)) std::abort();

    // Each consumer must observe exactly its routed item.
    std::optional<int> r0 = c0.try_pop();
    if (!r0 || *r0 != 1000) std::abort();
    std::optional<int> r1 = c1.try_pop();
    if (!r1 || *r1 != 2000) std::abort();
    std::optional<int> r2 = c2.try_pop();
    if (!r2 || *r2 != 3000) std::abort();

    // All shards drained.
    if (c0.try_pop()) std::abort();
    if (c1.try_pop()) std::abort();
    if (c2.try_pop()) std::abort();
}

// num_producers / num_consumers / shard_capacity constants reach
// runtime correctly through the alias template.
static void test_runtime_capacity_constants() {
    static_assert(TestGrid::num_producers == kM);
    static_assert(TestGrid::num_consumers == kN);
    static_assert(TestGrid::shard_capacity == 64);
    volatile std::size_t m   = TestGrid::num_producers;
    volatile std::size_t n   = TestGrid::num_consumers;
    volatile std::size_t cap = TestGrid::shard_capacity;
    if (m != kM) std::abort();
    if (n != kN) std::abort();
    if (cap != 64) std::abort();
}

// Substrate-pointer identity — the fixy:: alias resolves to EXACTLY
// the substrate type, not a fixy-side wrapper.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        TestGrid,
        cc::PermissionedShardedGrid<int, kM, kN, 64,
                                    probes::V048TestUserTag,
                                    cc::RoundRobinRouting>>);
    TestGrid grid{};
    cc::PermissionedShardedGrid<int, kM, kN, 64,
                                probes::V048TestUserTag,
                                cc::RoundRobinRouting>* via_sub = &grid;
    TestGrid* via_fixy = via_sub;  // implicit ptr conversion only works if types match
    if (via_fixy != via_sub) std::abort();
}

// Non-default Routing alias instantiation reaches the substrate.
// AffinityRouting picks consumer = producer_id % N, so producer<1>
// always routes to consumer 1 % 3 = 1 (statically known from the type).
static void test_runtime_affinity_routing_alias() {
    using AffinityGrid = fsubstr::sharded_grid::PermissionedShardedGrid<
        int, kM, kN, 64, probes::V048TestUserTag,
        fsubstr::sharded_grid::AffinityRouting>;
    static_assert(std::is_same_v<
        AffinityGrid,
        cc::PermissionedShardedGrid<int, kM, kN, 64,
                                    probes::V048TestUserTag,
                                    cc::AffinityRouting>>);

    AffinityGrid grid{};
    auto whole = cs::mint_permission_root<typename AffinityGrid::whole_tag>();
    auto perms = cs::mint_grid_permissions<
        typename AffinityGrid::whole_tag, kM, kN>(std::move(whole));

    auto p1 = grid.template producer<1>(
        std::move(std::get<1>(perms.producers)));
    auto c1 = grid.template consumer<1>(
        std::move(std::get<1>(perms.consumers)));

    // Producer 1 + AffinityRouting → consumer 1 % 3 == 1.
    if (!p1.try_push(7777)) std::abort();
    std::optional<int> r = c1.try_pop();
    if (!r || *r != 7777) std::abort();
}

// ProtocolType aliases unchanged from pre-V-048 surface — re-verify
// at runtime scope (compile-time witness lives in Substr.h's U-103
// block already; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::sharded_grid::ProducerProto<int>;
    using FixyCons = fsubstr::sharded_grid::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::sharded_grid_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::sharded_grid_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

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
