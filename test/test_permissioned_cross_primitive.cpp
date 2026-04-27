// ═══════════════════════════════════════════════════════════════════
// test_permissioned_cross_primitive — sentinel TU for FOUND-A25
//
// Exercises the *unified* Permissioned* API surface across all six
// wrappers (Spsc, Mpsc, Mpmc, ShardedGrid, ChaseLevDeque, Snapshot)
// in one TU.  The audit (FOUND-A24) standardised:
//
//   * Every wrapper exposes typedefs: value_type, user_tag, whole_tag.
//   * Every wrapper exposes role-tag aliases (varies by domain).
//   * Every wrapper exposes a mode-transition primitive:
//       - Pool-based:  with_drained_access(Body) -> bool
//       - Linear-tok:  with_recombined_access(Permission<whole>&&, Body)
//                              -> Permission<whole>
//   * Every wrapper exposes is_exclusive_active() -> bool.
//
// This TU compiles them all together with a single set of UserTags
// and verifies the unified surface compiles uniformly.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
    } while (0)

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// Per-wrapper UserTags so each instance has its own permission tree.
struct SpscUserTag      {};
struct MpscUserTag      {};
struct MpmcUserTag      {};
struct GridUserTag      {};
struct DequeUserTag     {};
struct SnapshotUserTag  {};

// ── Concept-style detection of the unified API surface ──────────────
//
// These concepts compile-check that each wrapper exposes the methods
// the audit standardised on.  If any wrapper drops a method later, this
// test fails to compile with a clear diagnostic naming the wrapper.

template <typename Ch>
concept HasUnifiedTypedefs =
    requires { typename Ch::value_type; }
 && requires { typename Ch::user_tag; }
 && requires { typename Ch::whole_tag; };

template <typename Ch>
concept HasIsExclusiveActive =
    requires(const Ch& c) { { c.is_exclusive_active() } -> std::same_as<bool>; };

template <typename Ch>
concept HasPoolDrainedAccess =
    requires(Ch& c) { { c.with_drained_access([]() noexcept {}) } -> std::same_as<bool>; };

template <typename Ch>
concept HasLinearRecombinedAccess =
    requires(Ch& c, Permission<typename Ch::whole_tag> p) {
        { c.with_recombined_access(std::move(p), []() noexcept {}) }
            -> std::same_as<Permission<typename Ch::whole_tag>>;
    };

// ── Tier 1: typedefs uniform across all six wrappers ────────────────

void test_unified_typedefs_present() {
    using Spsc  = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc  = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc  = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Grid  = PermissionedShardedGrid<int, 2, 2, 64, GridUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap  = PermissionedSnapshot<int, SnapshotUserTag>;

    static_assert(HasUnifiedTypedefs<Spsc>,
                  "PermissionedSpscChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Mpsc>,
                  "PermissionedMpscChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Mpmc>,
                  "PermissionedMpmcChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Grid>,
                  "PermissionedShardedGrid: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Deque>,
                  "PermissionedChaseLevDeque: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Snap>,
                  "PermissionedSnapshot: missing unified typedef");

    static_assert(std::is_same_v<typename Spsc::value_type,  int>);
    static_assert(std::is_same_v<typename Mpsc::value_type,  int>);
    static_assert(std::is_same_v<typename Mpmc::value_type,  int>);
    static_assert(std::is_same_v<typename Grid::value_type,  int>);
    static_assert(std::is_same_v<typename Deque::value_type, int>);
    static_assert(std::is_same_v<typename Snap::value_type,  int>);
}

// ── Tier 2: is_exclusive_active uniform across all six ──────────────

void test_unified_is_exclusive_active() {
    using Spsc  = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc  = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc  = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap  = PermissionedSnapshot<int, SnapshotUserTag>;

    static_assert(HasIsExclusiveActive<Spsc>,
                  "PermissionedSpscChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Mpsc>,
                  "PermissionedMpscChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Mpmc>,
                  "PermissionedMpmcChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Deque>,
                  "PermissionedChaseLevDeque: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Snap>,
                  "PermissionedSnapshot: missing is_exclusive_active");

    Spsc spsc;
    Mpsc mpsc;
    Mpmc mpmc;
    Deque deque;
    Snap snap{0};
    CRUCIBLE_TEST_REQUIRE(spsc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(mpsc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(mpmc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(deque.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(snap.is_exclusive_active() == false);
}

// ── Tier 3: with_drained_access uniform across pool-based ───────────

void test_unified_with_drained_access_pool_based() {
    using Mpsc  = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc  = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap  = PermissionedSnapshot<int, SnapshotUserTag>;

    static_assert(HasPoolDrainedAccess<Mpsc>,
                  "PermissionedMpscChannel: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Mpmc>,
                  "PermissionedMpmcChannel: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Deque>,
                  "PermissionedChaseLevDeque: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Snap>,
                  "PermissionedSnapshot: missing with_drained_access");

    Mpsc mpsc;
    Mpmc mpmc;
    Deque deque;
    Snap snap{0};

    bool body_ran;

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(
        mpsc.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(
        mpmc.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(
        deque.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(
        snap.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);
}

// ── Tier 4: with_recombined_access for Spsc (linear-token variant) ──

void test_spsc_with_recombined_access() {
    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;

    static_assert(HasLinearRecombinedAccess<Spsc>,
                  "PermissionedSpscChannel: missing with_recombined_access");

    Spsc spsc;

    auto whole = permission_root_mint<typename Spsc::whole_tag>();
    bool body_ran = false;
    auto returned = spsc.with_recombined_access(
        std::move(whole),
        [&]() noexcept { body_ran = true; });

    CRUCIBLE_TEST_REQUIRE(body_ran);
    // Returned permission can be re-split for the next session.
    auto [pp, cp] = permission_split<typename Spsc::producer_tag,
                                      typename Spsc::consumer_tag>(
        std::move(returned));
    auto producer = spsc.producer(std::move(pp));
    auto consumer = spsc.consumer(std::move(cp));
    CRUCIBLE_TEST_REQUIRE(producer.try_push(42));
    auto popped = consumer.try_pop();
    CRUCIBLE_TEST_REQUIRE(popped.has_value());
    CRUCIBLE_TEST_REQUIRE(*popped == 42);
}

// ── Tier 5: cross-primitive composition — three wrappers in series ──
//
// Models a pipeline: Spsc producer → Mpsc consumer (via shared int) →
// Snapshot publisher.  Each stage has its own permission discipline;
// the test validates that handle types compose without surprise.

void test_pipeline_three_wrappers() {
    using Spsc = PermissionedSpscChannel<int, 32, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 32, MpscUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;

    Spsc spsc;
    Mpsc mpsc;
    Snap snap{0};

    auto spsc_whole = permission_root_mint<typename Spsc::whole_tag>();
    auto mpsc_whole = permission_root_mint<typename Mpsc::whole_tag>();
    auto snap_whole = permission_root_mint<typename Snap::whole_tag>();

    auto [spsc_pp, spsc_cp] = permission_split<
        typename Spsc::producer_tag,
        typename Spsc::consumer_tag>(std::move(spsc_whole));
    // Mpsc: producer side is POOL (no Permission arg, returns optional);
    //       consumer side is LINEAR (takes Permission).  Caller only
    //       holds the linear Permission — pool side mints its root from
    //       the channel itself at construction.
    auto [mpsc_pp, mpsc_cp] = permission_split<
        typename Mpsc::producer_tag,
        typename Mpsc::consumer_tag>(std::move(mpsc_whole));
    (void)mpsc_pp;  // pool side — Permission is not consumed by handle factory
    // Snapshot: writer side is LINEAR; reader side is POOL.
    auto [snap_wp, snap_rp] = permission_split<
        typename Snap::writer_tag,
        typename Snap::reader_tag>(std::move(snap_whole));
    (void)snap_rp;  // pool side

    auto spsc_prod = spsc.producer(std::move(spsc_pp));
    auto spsc_cons = spsc.consumer(std::move(spsc_cp));
    auto mpsc_prod_opt = mpsc.producer();        // pool side — no Permission arg
    auto mpsc_cons     = mpsc.consumer(std::move(mpsc_cp));  // linear
    auto snap_writ     = snap.writer(std::move(snap_wp));    // linear

    CRUCIBLE_TEST_REQUIRE(mpsc_prod_opt.has_value());
    auto& mpsc_prod = *mpsc_prod_opt;

    // Push a value through SPSC → consume → push through MPSC →
    // consume → publish to snapshot.
    CRUCIBLE_TEST_REQUIRE(spsc_prod.try_push(7));
    auto from_spsc = spsc_cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(from_spsc.has_value() && *from_spsc == 7);

    CRUCIBLE_TEST_REQUIRE(mpsc_prod.try_push(*from_spsc * 2));
    auto from_mpsc = mpsc_cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(from_mpsc.has_value() && *from_mpsc == 14);

    snap_writ.publish(*from_mpsc);

    // Reader observes the published value via a fresh share.
    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(reader_opt->load() == 14);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissioned_cross_primitive\n");

    run_test("test_unified_typedefs_present",
             test_unified_typedefs_present);
    run_test("test_unified_is_exclusive_active",
             test_unified_is_exclusive_active);
    run_test("test_unified_with_drained_access_pool_based",
             test_unified_with_drained_access_pool_based);
    run_test("test_spsc_with_recombined_access",
             test_spsc_with_recombined_access);
    run_test("test_pipeline_three_wrappers",
             test_pipeline_three_wrappers);

    std::fprintf(stderr, "%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
