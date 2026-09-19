// The permissioned channel wrappers are written independently of each
// other, so nothing in any one of them enforces that they present the
// same surface.  This translation unit instantiates all of them side by
// side and asks each for the same typedefs, the same mode-transition
// primitive, and the same diagnostics, which is the only place a
// divergence between two wrappers becomes a build failure.
//
// The wrappers split into two families by how they hand out exclusive
// access.  A pool-based wrapper drains itself and runs the body; a
// linear-token wrapper takes the whole-tag permission, runs the body,
// and hands the permission back.  Every wrapper belongs to exactly one
// family, and the checks below pin that as well.

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/traits/Concepts.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
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

// One tag per wrapper, so that no two instances share a permission tree
// and a leak between them would show up as a compile error.
struct SpscUserTag {};
struct MpscUserTag {};
struct MpmcUserTag {};
struct GridUserTag {};
struct DequeUserTag {};
struct SnapshotUserTag {};
struct CalendarUserTag {};

struct CrossKeyJob {
    std::uint64_t deadline_ns = 0;
    std::uint64_t payload = 0;
};
struct CrossKeyExtract {
    static std::uint64_t key(const CrossKeyJob& j) noexcept { return j.deadline_ns; }
};

// Concepts rather than direct member access, so that a wrapper which
// drops a method fails at the static_assert that names it instead of
// somewhere inside a call expression.
template <typename Ch>
concept HasUnifiedTypedefs = requires { typename Ch::value_type; } && requires { typename Ch::user_tag; }
                          && requires { typename Ch::whole_tag; };

template <typename Ch>
concept HasIsExclusiveActive = requires(const Ch& c) {
    { c.is_exclusive_active() } -> std::same_as<bool>;
};

template <typename Ch>
concept HasPoolDrainedAccess = requires(Ch& c) {
    {
        c.with_drained_access([]() noexcept {})
    } -> std::same_as<bool>;
};

template <typename Ch>
concept HasLinearRecombinedAccess = requires(Ch& c, Permission<typename Ch::whole_tag> p) {
    {
        c.with_recombined_access(std::move(p), []() noexcept {})
    } -> std::same_as<Permission<typename Ch::whole_tag>>;
};

// Snapshot is the one wrapper this does not apply to.  Its observable
// state is the latest value and its version, not a queued count, so a
// size or a capacity would have no meaning on it.
template <typename H>
concept HasFifoHandleDiagnostics = requires(const H& h) {
    { h.size_approx() } -> std::same_as<std::size_t>;
    { h.empty_approx() } -> std::same_as<bool>;
    { H::capacity() } -> std::same_as<std::size_t>;
};

void test_unified_typedefs_present() {
    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Grid = PermissionedShardedGrid<int, 2, 2, 64, GridUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;
    using Calendar = PermissionedCalendarGrid<CrossKeyJob, /*M*/ 2, /*Buckets*/ 8,
                                              /*Cap*/ 16, CrossKeyExtract,
                                              /*QuantumNs*/ 1000000ULL, CalendarUserTag>;

    static_assert(HasUnifiedTypedefs<Spsc>, "PermissionedSpscChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Mpsc>, "PermissionedMpscChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Mpmc>, "PermissionedMpmcChannel: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Grid>, "PermissionedShardedGrid: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Deque>, "PermissionedChaseLevDeque: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Snap>, "PermissionedSnapshot: missing unified typedef");
    static_assert(HasUnifiedTypedefs<Calendar>, "PermissionedCalendarGrid: missing unified typedef");

    static_assert(std::is_same_v<typename Spsc::value_type, int>);
    static_assert(std::is_same_v<typename Mpsc::value_type, int>);
    static_assert(std::is_same_v<typename Mpmc::value_type, int>);
    static_assert(std::is_same_v<typename Grid::value_type, int>);
    static_assert(std::is_same_v<typename Deque::value_type, int>);
    static_assert(std::is_same_v<typename Snap::value_type, int>);
    static_assert(std::is_same_v<typename Calendar::value_type, CrossKeyJob>);
}

void test_unified_is_exclusive_active() {
    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;
    using Calendar = PermissionedCalendarGrid<CrossKeyJob, 2, 8, 16, CrossKeyExtract, 1000000ULL, CalendarUserTag>;

    static_assert(HasIsExclusiveActive<Spsc>, "PermissionedSpscChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Mpsc>, "PermissionedMpscChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Mpmc>, "PermissionedMpmcChannel: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Deque>, "PermissionedChaseLevDeque: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Snap>, "PermissionedSnapshot: missing is_exclusive_active");
    static_assert(HasIsExclusiveActive<Calendar>, "PermissionedCalendarGrid: missing is_exclusive_active");

    Spsc spsc;
    Mpsc mpsc;
    Mpmc mpmc;
    Deque deque;
    Snap snap{0};
    Calendar cal;
    CRUCIBLE_TEST_REQUIRE(spsc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(mpsc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(mpmc.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(deque.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(snap.is_exclusive_active() == false);
    CRUCIBLE_TEST_REQUIRE(cal.is_exclusive_active() == false);
}

void test_unified_with_drained_access_pool_based() {
    using Mpsc = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;

    static_assert(HasPoolDrainedAccess<Mpsc>, "PermissionedMpscChannel: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Mpmc>, "PermissionedMpmcChannel: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Deque>, "PermissionedChaseLevDeque: missing with_drained_access");
    static_assert(HasPoolDrainedAccess<Snap>, "PermissionedSnapshot: missing with_drained_access");

    Mpsc mpsc;
    Mpmc mpmc;
    Deque deque;
    Snap snap{0};

    bool body_ran;

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(mpsc.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(mpmc.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(deque.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);

    body_ran = false;
    CRUCIBLE_TEST_REQUIRE(snap.with_drained_access([&]() noexcept { body_ran = true; }));
    CRUCIBLE_TEST_REQUIRE(body_ran);
}

// The diagnostics have to be present on every handle of every FIFO
// wrapper, not just on the channel.  A producer handle that reports a
// size while the matching consumer handle does not is the shape this
// check exists to forbid.

void test_unified_handle_diagnostics() {
    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Grid = PermissionedShardedGrid<int, 2, 2, 64, GridUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Calendar = PermissionedCalendarGrid<CrossKeyJob, 2, 8, 16, CrossKeyExtract, 1000000ULL, CalendarUserTag>;

    static_assert(HasFifoHandleDiagnostics<typename Spsc::ProducerHandle>,
                  "PermissionedSpscChannel::ProducerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Spsc::ConsumerHandle>,
                  "PermissionedSpscChannel::ConsumerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Mpsc::ProducerHandle>,
                  "PermissionedMpscChannel::ProducerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Mpsc::ConsumerHandle>,
                  "PermissionedMpscChannel::ConsumerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Mpmc::ProducerHandle>,
                  "PermissionedMpmcChannel::ProducerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Mpmc::ConsumerHandle>,
                  "PermissionedMpmcChannel::ConsumerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Grid::template ProducerHandle<0>>,
                  "PermissionedShardedGrid::ProducerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Grid::template ConsumerHandle<0>>,
                  "PermissionedShardedGrid::ConsumerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Deque::OwnerHandle>,
                  "PermissionedChaseLevDeque::OwnerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Deque::ThiefHandle>,
                  "PermissionedChaseLevDeque::ThiefHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Calendar::template ProducerHandle<0>>,
                  "PermissionedCalendarGrid::ProducerHandle: diagnostic trio missing");
    static_assert(HasFifoHandleDiagnostics<typename Calendar::ConsumerHandle>,
                  "PermissionedCalendarGrid::ConsumerHandle: diagnostic trio missing");

    // The concept only proves the methods exist.  Reading them before
    // and after one push proves they are wired to the channel rather
    // than returning a constant.
    Spsc spsc;
    auto sw = mint_permission_root<typename Spsc::whole_tag>();
    auto [spp, scp] = mint_permission_split<typename Spsc::producer_tag, typename Spsc::consumer_tag>(std::move(sw));
    auto sp = spsc.producer(std::move(spp));
    auto sc = spsc.consumer(std::move(scp));
    CRUCIBLE_TEST_REQUIRE(sp.empty_approx() && sc.empty_approx());
    CRUCIBLE_TEST_REQUIRE(sp.size_approx() == 0u);
    CRUCIBLE_TEST_REQUIRE(sc.size_approx() == 0u);
    CRUCIBLE_TEST_REQUIRE(sp.try_push(99));
    CRUCIBLE_TEST_REQUIRE(!sp.empty_approx() && !sc.empty_approx());
    CRUCIBLE_TEST_REQUIRE(sp.size_approx() == 1u);
    CRUCIBLE_TEST_REQUIRE(sc.size_approx() == 1u);
}

void test_spsc_with_recombined_access() {
    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;

    static_assert(HasLinearRecombinedAccess<Spsc>, "PermissionedSpscChannel: missing with_recombined_access");

    Spsc spsc;

    auto whole = mint_permission_root<typename Spsc::whole_tag>();
    bool body_ran = false;
    auto returned = spsc.with_recombined_access(std::move(whole), [&]() noexcept { body_ran = true; });

    CRUCIBLE_TEST_REQUIRE(body_ran);
    // Re-splitting the returned permission is what shows the token came
    // back whole rather than partially consumed.
    auto [pp, cp] =
        mint_permission_split<typename Spsc::producer_tag, typename Spsc::consumer_tag>(std::move(returned));
    auto producer = spsc.producer(std::move(pp));
    auto consumer = spsc.consumer(std::move(cp));
    CRUCIBLE_TEST_REQUIRE(producer.try_push(42));
    auto popped = consumer.try_pop();
    CRUCIBLE_TEST_REQUIRE(popped.has_value());
    CRUCIBLE_TEST_REQUIRE(*popped == 42);
}

// The calendar grid has M producer slots and one consumer slot, all of
// them linear, which is why it takes the recombined-permission shape
// rather than the drained one.

void test_calendar_with_recombined_access() {
    using Calendar = PermissionedCalendarGrid<CrossKeyJob, 2, 8, 16, CrossKeyExtract, 1000000ULL, CalendarUserTag>;
    static_assert(HasLinearRecombinedAccess<Calendar>, "PermissionedCalendarGrid: missing with_recombined_access");

    Calendar cal;

    auto whole = mint_permission_root<typename Calendar::whole_tag>();
    bool body_ran = false;
    auto returned = cal.with_recombined_access(std::move(whole), [&]() noexcept { body_ran = true; });
    CRUCIBLE_TEST_REQUIRE(body_ran);

    // Re-split through the same factory the handles use, then push and
    // pop once, so that the channel is shown working after the mode
    // transition and not merely still constructible.
    auto perms = mint_grid_permissions<typename Calendar::whole_tag, 2, 1>(std::move(returned));
    auto p0 = cal.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = cal.consumer(std::move(std::get<0>(perms.consumers)));
    CRUCIBLE_TEST_REQUIRE(p0.try_push(CrossKeyJob{.deadline_ns = 0, .payload = 99}));
    auto popped = cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(popped.has_value());
    CRUCIBLE_TEST_REQUIRE(popped->payload == 99);
}

// The concepts above are local to this file.  The ones below ship with
// the wrappers, and production code dispatches on them, so the two sets
// have to agree about which family each wrapper belongs to.

void test_formal_concepts_satisfied() {
    using namespace crucible::concurrent::traits;

    using Spsc = PermissionedSpscChannel<int, 64, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 64, MpscUserTag>;
    using Mpmc = PermissionedMpmcChannel<int, 64, MpmcUserTag>;
    using Grid = PermissionedShardedGrid<int, 2, 2, 64, GridUserTag>;
    using Deque = PermissionedChaseLevDeque<int, 64, DequeUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;
    using Calendar = PermissionedCalendarGrid<CrossKeyJob, 2, 8, 16, CrossKeyExtract, 1000000ULL, CalendarUserTag>;

    static_assert(PermissionedChannel<Spsc>);
    static_assert(PermissionedChannel<Mpsc>);
    static_assert(PermissionedChannel<Mpmc>);
    static_assert(PermissionedChannel<Grid>);
    static_assert(PermissionedChannel<Deque>);
    static_assert(PermissionedChannel<Snap>);
    static_assert(PermissionedChannel<Calendar>);

    // The two families are exclusive, so each wrapper is asserted in
    // both directions rather than only in the one it belongs to.
    static_assert(LinearOnlyChannel<Spsc>);
    static_assert(!PoolBasedChannel<Spsc>);
    static_assert(PoolBasedChannel<Mpsc>);
    static_assert(!LinearOnlyChannel<Mpsc>);
    static_assert(PoolBasedChannel<Mpmc>);
    static_assert(!LinearOnlyChannel<Mpmc>);
    static_assert(LinearOnlyChannel<Grid>);
    static_assert(!PoolBasedChannel<Grid>);
    static_assert(PoolBasedChannel<Deque>);
    static_assert(!LinearOnlyChannel<Deque>);
    static_assert(PoolBasedChannel<Snap>);
    static_assert(!LinearOnlyChannel<Snap>);
    static_assert(LinearOnlyChannel<Calendar>);
    static_assert(!PoolBasedChannel<Calendar>);

    static_assert(FifoChannel<Spsc>);
    static_assert(FifoChannel<Mpsc>);
    static_assert(FifoChannel<Mpmc>);
    static_assert(FifoChannel<Grid>);
    static_assert(FifoChannel<Deque>);
    static_assert(!FifoChannel<Snap>);
    static_assert(FifoChannel<Calendar>);
}

// A value travels through three wrappers in series, each with a
// different permission discipline.  The checks above hold each wrapper
// to the shared surface one at a time; this one shows the handles
// actually compose when chained.

void test_pipeline_three_wrappers() {
    using Spsc = PermissionedSpscChannel<int, 32, SpscUserTag>;
    using Mpsc = PermissionedMpscChannel<int, 32, MpscUserTag>;
    using Snap = PermissionedSnapshot<int, SnapshotUserTag>;

    Spsc spsc;
    Mpsc mpsc;
    Snap snap{0};

    auto spsc_whole = mint_permission_root<typename Spsc::whole_tag>();
    auto mpsc_whole = mint_permission_root<typename Mpsc::whole_tag>();
    auto snap_whole = mint_permission_root<typename Snap::whole_tag>();

    auto [spsc_pp, spsc_cp] =
        mint_permission_split<typename Spsc::producer_tag, typename Spsc::consumer_tag>(std::move(spsc_whole));
    // The caller holds only the consumer permission here.  The producer
    // side is pool-based and mints its own root from the channel at
    // construction, which is why nothing splits out a producer token.
    auto [mpsc_pp, mpsc_cp] =
        mint_permission_split<typename Mpsc::producer_tag, typename Mpsc::consumer_tag>(std::move(mpsc_whole));
    (void)mpsc_pp;
    auto [snap_wp, snap_rp] =
        mint_permission_split<typename Snap::writer_tag, typename Snap::reader_tag>(std::move(snap_whole));
    (void)snap_rp;

    auto spsc_prod = spsc.producer(std::move(spsc_pp));
    auto spsc_cons = spsc.consumer(std::move(spsc_cp));
    auto mpsc_prod_opt = mpsc.producer();
    auto mpsc_cons = mpsc.consumer(std::move(mpsc_cp));
    auto snap_writ = snap.writer(std::move(snap_wp));

    CRUCIBLE_TEST_REQUIRE(mpsc_prod_opt.has_value());
    auto& mpsc_prod = *mpsc_prod_opt;

    // The value is doubled between the two queues so that a stage which
    // silently dropped its input would leave the wrong number at the
    // end rather than the right one.
    CRUCIBLE_TEST_REQUIRE(spsc_prod.try_push(7));
    auto from_spsc = spsc_cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(from_spsc.has_value() && *from_spsc == 7);

    CRUCIBLE_TEST_REQUIRE(mpsc_prod.try_push(*from_spsc * 2));
    auto from_mpsc = mpsc_cons.try_pop();
    CRUCIBLE_TEST_REQUIRE(from_mpsc.has_value() && *from_mpsc == 14);

    snap_writ.publish(*from_mpsc);

    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(reader_opt->load() == 14);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissioned_cross_primitive\n");

    run_test("test_unified_typedefs_present", test_unified_typedefs_present);
    run_test("test_unified_is_exclusive_active", test_unified_is_exclusive_active);
    run_test("test_unified_with_drained_access_pool_based", test_unified_with_drained_access_pool_based);
    run_test("test_unified_handle_diagnostics", test_unified_handle_diagnostics);
    run_test("test_spsc_with_recombined_access", test_spsc_with_recombined_access);
    run_test("test_calendar_with_recombined_access", test_calendar_with_recombined_access);
    run_test("test_formal_concepts_satisfied", test_formal_concepts_satisfied);
    run_test("test_pipeline_three_wrappers", test_pipeline_three_wrappers);

    std::fprintf(stderr, "%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
