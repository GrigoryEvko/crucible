#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/ChainEdge.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/_Permission.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A file-local tag gives this test a fresh Whole/Signaler/Waiter triple.
struct V052TestUserTag {};

}  // namespace probes

using TestEdge = fsubstr::chainedge::PermissionedChainEdge<cc::VendorBackend::CPU, probes::V052TestUserTag>;
using TestEdgeNv = fsubstr::chainedge::PermissionedChainEdge<cc::VendorBackend::NV, probes::V052TestUserTag>;

static_assert(std::is_same_v<TestEdge, cc::PermissionedChainEdge<cc::VendorBackend::CPU, probes::V052TestUserTag>>,
              "fixy::substr::chainedge::PermissionedChainEdge<CPU,_> must alias "
              "the substrate.");

static_assert(std::is_same_v<TestEdgeNv, cc::PermissionedChainEdge<cc::VendorBackend::NV, probes::V052TestUserTag>>,
              "fixy::substr::chainedge::PermissionedChainEdge<NV,_> must alias "
              "the substrate's non-default-backend variant.");

static_assert(std::is_same_v<fsubstr::chainedge::VendorBackend, cc::VendorBackend>,
              "fixy::substr::chainedge::VendorBackend must alias the substrate's "
              "VendorBackend.");
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::CPU) == static_cast<int>(cc::VendorBackend::CPU));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::NV) == static_cast<int>(cc::VendorBackend::NV));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::AMD) == static_cast<int>(cc::VendorBackend::AMD));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::TPU) == static_cast<int>(cc::VendorBackend::TPU));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::TRN) == static_cast<int>(cc::VendorBackend::TRN));

static_assert(std::is_same_v<fsubstr::chainedge::chainedge_tag::Whole<probes::V052TestUserTag>,
                             cc::chainedge_tag::Whole<probes::V052TestUserTag>>);
static_assert(std::is_same_v<fsubstr::chainedge::chainedge_tag::Signaler<probes::V052TestUserTag>,
                             cc::chainedge_tag::Signaler<probes::V052TestUserTag>>);
static_assert(std::is_same_v<fsubstr::chainedge::chainedge_tag::Waiter<probes::V052TestUserTag>,
                             cc::chainedge_tag::Waiter<probes::V052TestUserTag>>);

static_assert(
    std::is_same_v<typename TestEdge::whole_tag, fsubstr::chainedge::chainedge_tag::Whole<probes::V052TestUserTag>>);
static_assert(std::is_same_v<typename TestEdge::signaler_tag,
                             fsubstr::chainedge::chainedge_tag::Signaler<probes::V052TestUserTag>>);
static_assert(
    std::is_same_v<typename TestEdge::waiter_tag, fsubstr::chainedge::chainedge_tag::Waiter<probes::V052TestUserTag>>);

static_assert(fsubstr::chainedge::ChainEdgeSessionSurface<TestEdge>);
static_assert(fsubstr::chainedge::ChainEdgeSessionSurface<TestEdgeNv>);

static_assert(std::is_same_v<typename TestEdge::value_type, cc::SemaphoreSignal>);
static_assert(std::is_same_v<typename TestEdge::value_type, fsubstr::chainedge::Signal>);

static_assert(TestEdge::backend == cc::VendorBackend::CPU);
static_assert(TestEdgeNv::backend == cc::VendorBackend::NV);

static_assert(
    std::is_same_v<fsubstr::chainedge::SignalerProto, ::crucible::safety::proto::chainedge_session::SignalerProto>);
static_assert(
    std::is_same_v<fsubstr::chainedge::WaiterProto, ::crucible::safety::proto::chainedge_session::WaiterProto>);

namespace {

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)

template <typename Body>
void run_test(const char* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

// A non-default signal_value, so the strict expected-signal gate runs.
struct EdgeFixture {
    static constexpr cc::PlanId upstream{42};
    static constexpr cc::PlanId downstream{84};
    static constexpr cc::ChainEdgeId edge_id{17};
    static constexpr std::uint64_t signal_value = 7;
};

template <typename Edge>
[[nodiscard]] auto fresh_chainedge_perms() {
    auto whole = cs::mint_permission_root<typename Edge::whole_tag>();
    return cs::mint_permission_split<typename Edge::signaler_tag, typename Edge::waiter_tag>(std::move(whole));
}

}  // namespace

static void test_runtime_construct_and_handles() {
    TestEdge edge{EdgeFixture::upstream, EdgeFixture::downstream, EdgeFixture::edge_id, EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter = edge.waiter(std::move(wp));
    (void)signaler;
    (void)waiter;

    CRUCIBLE_TEST_REQUIRE(signaler.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 0);
}

static void test_runtime_signal_wait_round_trip() {
    TestEdge edge{EdgeFixture::upstream, EdgeFixture::downstream, EdgeFixture::edge_id, EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter = edge.waiter(std::move(wp));

    const cc::SemaphoreSignal expected = waiter.expected_signal();
    CRUCIBLE_TEST_REQUIRE(!waiter.try_wait(expected));
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 0);

    const cc::SemaphoreSignal emitted = signaler.signal();
    CRUCIBLE_TEST_REQUIRE(emitted.value == EdgeFixture::signal_value);
    CRUCIBLE_TEST_REQUIRE(signaler.current_value() == EdgeFixture::signal_value);

    CRUCIBLE_TEST_REQUIRE(waiter.try_wait(emitted));
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == EdgeFixture::signal_value);
}

static void test_runtime_expected_signal_propagates() {
    TestEdge edge{EdgeFixture::upstream, EdgeFixture::downstream, EdgeFixture::edge_id, EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter = edge.waiter(std::move(wp));

    const cc::SemaphoreSignal sig = signaler.expected_signal();
    CRUCIBLE_TEST_REQUIRE(sig.edge == EdgeFixture::edge_id);
    CRUCIBLE_TEST_REQUIRE(sig.upstream == EdgeFixture::upstream);
    CRUCIBLE_TEST_REQUIRE(sig.downstream == EdgeFixture::downstream);
    CRUCIBLE_TEST_REQUIRE(sig.value == EdgeFixture::signal_value);
    CRUCIBLE_TEST_REQUIRE(sig.backend == cc::VendorBackend::CPU);

    const cc::SemaphoreSignal wsig = waiter.expected_signal();
    CRUCIBLE_TEST_REQUIRE(wsig.edge == sig.edge);
    CRUCIBLE_TEST_REQUIRE(wsig.upstream == sig.upstream);
    CRUCIBLE_TEST_REQUIRE(wsig.downstream == sig.downstream);
    CRUCIBLE_TEST_REQUIRE(wsig.value == sig.value);
    CRUCIBLE_TEST_REQUIRE(wsig.backend == sig.backend);

    // A mismatched signal is dropped silently.  The only observable is
    // that the counter does not advance.
    cc::SemaphoreSignal forged = sig;
    forged.edge = cc::ChainEdgeId{forged.edge.raw() + 1};
    signaler.signal(forged);
    CRUCIBLE_TEST_REQUIRE(signaler.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(!waiter.try_wait(sig));
}

static void test_runtime_reset_under_quiescence() {
    TestEdge edge{EdgeFixture::upstream, EdgeFixture::downstream, EdgeFixture::edge_id, EdgeFixture::signal_value};

    auto whole1 = cs::mint_permission_root<TestEdge::whole_tag>();
    auto [sp1, wp1] = cs::mint_permission_split<TestEdge::signaler_tag, TestEdge::waiter_tag>(std::move(whole1));
    auto signaler1 = edge.signaler(std::move(sp1));
    auto waiter1 = edge.waiter(std::move(wp1));
    const cc::SemaphoreSignal sig1 = signaler1.signal();
    CRUCIBLE_TEST_REQUIRE(waiter1.try_wait(sig1));
    CRUCIBLE_TEST_REQUIRE(waiter1.current_value() == EdgeFixture::signal_value);

    // The Whole permission must be re-minted: the split halves live in
    // the handles above, which are not dropped until scope exit.
    auto whole2 = cs::mint_permission_root<TestEdge::whole_tag>();
    whole2 = edge.reset_under_quiescence(std::move(whole2));

    auto [sp2, wp2] = cs::mint_permission_split<TestEdge::signaler_tag, TestEdge::waiter_tag>(std::move(whole2));
    auto signaler2 = edge.signaler(std::move(sp2));
    auto waiter2 = edge.waiter(std::move(wp2));
    CRUCIBLE_TEST_REQUIRE(waiter2.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(!waiter2.try_wait(waiter2.expected_signal()));

    const cc::SemaphoreSignal sig2 = signaler2.signal();
    CRUCIBLE_TEST_REQUIRE(waiter2.try_wait(sig2));
}

// Every vendor backend delegates to the CPU oracle, so this proves
// nothing about NV execution.  What it does prove is that the backend
// travels as part of the signal identity.
static void test_runtime_nv_backend_variant() {
    TestEdgeNv edge_nv{EdgeFixture::upstream, EdgeFixture::downstream, EdgeFixture::edge_id, EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdgeNv>();
    auto signaler_nv = edge_nv.signaler(std::move(sp));
    auto waiter_nv = edge_nv.waiter(std::move(wp));

    const cc::SemaphoreSignal sig = signaler_nv.expected_signal();
    CRUCIBLE_TEST_REQUIRE(sig.backend == cc::VendorBackend::NV);

    const cc::SemaphoreSignal emitted = signaler_nv.signal();
    CRUCIBLE_TEST_REQUIRE(emitted.backend == cc::VendorBackend::NV);
    CRUCIBLE_TEST_REQUIRE(waiter_nv.try_wait(emitted));
    CRUCIBLE_TEST_REQUIRE(waiter_nv.current_value() == EdgeFixture::signal_value);
}

static void test_runtime_substrate_identity() {
    static_assert(!std::is_copy_constructible_v<TestEdge>);
    static_assert(!std::is_move_constructible_v<TestEdge>);
    static_assert(!std::is_copy_assignable_v<TestEdge>);
    static_assert(!std::is_move_assignable_v<TestEdge>);

    static_assert(TestEdge::backend == cc::VendorBackend::CPU);
    static_assert(TestEdgeNv::backend == cc::VendorBackend::NV);

    static_assert(std::is_same_v<typename TestEdge::value_type, cc::SemaphoreSignal>);
    static_assert(std::is_same_v<typename TestEdge::value_type, fsubstr::chainedge::Signal>);
}

int main() {
    std::fprintf(stderr, "test_fixy_substr_chainedge_permissioned_chain_edge: "
                         "starting V-052 runtime witnesses\n");

    run_test("construct_and_handles", test_runtime_construct_and_handles);
    run_test("signal_wait_round_trip", test_runtime_signal_wait_round_trip);
    run_test("expected_signal_propagates", test_runtime_expected_signal_propagates);
    run_test("reset_under_quiescence", test_runtime_reset_under_quiescence);
    run_test("nv_backend_variant", test_runtime_nv_backend_variant);
    run_test("substrate_identity", test_runtime_substrate_identity);

    std::fprintf(stderr,
                 "test_fixy_substr_chainedge_permissioned_chain_edge: "
                 "%d/%d runtime witnesses passed\n",
                 total_passed, total_passed + total_failed);

    return total_failed == 0 ? 0 : 1;
}
