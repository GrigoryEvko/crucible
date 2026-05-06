// GAPS-062 integration test for ChainEdge, PermissionedChainEdge, and
// ChainEdgeSession.

#include <cstdio>
#include <utility>

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChainEdgeSession.h>

namespace {

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                  \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr,                                      \
                "  REQUIRE FAILED: %s @ %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                           \
            ++total_failed;                                           \
            return;                                                   \
        }                                                             \
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

template <::crucible::concurrent::VendorBackend Backend>
void test_raw_chainedge_backend() {
    using namespace ::crucible::concurrent;

    ChainEdge<Backend> edge{PlanId{10}, PlanId{20}, ChainEdgeId{30}, 4};
    const SemaphoreSignal signal = edge.expected_signal();

    CRUCIBLE_TEST_REQUIRE(!edge.wait(signal));
    edge.signal(signal);
    CRUCIBLE_TEST_REQUIRE(edge.wait(signal));
    CRUCIBLE_TEST_REQUIRE(edge.current_value() == 4);
    CRUCIBLE_TEST_REQUIRE(signal.backend == Backend);
}

template <::crucible::concurrent::VendorBackend Backend>
void test_permissioned_chainedge_backend() {
    using namespace ::crucible::concurrent;
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;

    struct Tag {};
    using Edge = PermissionedChainEdge<Backend, Tag>;

    Edge edge{PlanId{11}, PlanId{21}, ChainEdgeId{31}, 5};
    auto whole = mint_permission_root<typename Edge::whole_tag>();
    auto [sp, wp] = mint_permission_split<typename Edge::signaler_tag,
                                          typename Edge::waiter_tag>(
        std::move(whole));

    auto signaler = edge.signaler(std::move(sp));
    auto waiter = edge.waiter(std::move(wp));
    const SemaphoreSignal signal = signaler.signal();

    CRUCIBLE_TEST_REQUIRE(waiter.try_wait(signal));
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 5);
    CRUCIBLE_TEST_REQUIRE(signal.edge == ChainEdgeId{31});
    CRUCIBLE_TEST_REQUIRE(signal.upstream == PlanId{11});
    CRUCIBLE_TEST_REQUIRE(signal.downstream == PlanId{21});
}

void test_typed_session_round_trip() {
    namespace ses = ::crucible::safety::proto::chainedge_session;
    using namespace ::crucible::concurrent;
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;

    struct Tag {};
    using Edge = PermissionedChainEdge<VendorBackend::CPU, Tag>;

    Edge edge{PlanId{12}, PlanId{22}, ChainEdgeId{32}, 6};
    auto whole = mint_permission_root<typename Edge::whole_tag>();
    auto [sp, wp] = mint_permission_split<typename Edge::signaler_tag,
                                          typename Edge::waiter_tag>(
        std::move(whole));
    auto signaler = ses::mint_chainedge_signaler<Edge>(edge, std::move(sp));
    auto waiter = ses::mint_chainedge_waiter<Edge>(edge, std::move(wp));

    auto signaler_psh = ses::mint_chainedge_signaler_session<Edge>(signaler);
    auto waiter_psh = ses::mint_chainedge_waiter_session<Edge>(waiter);

    const SemaphoreSignal signal = signaler.expected_signal();
    auto signaler_end =
        std::move(signaler_psh).send(signal, ses::signal_transport);
    auto [observed, waiter_end] =
        std::move(waiter_psh).recv(ses::wait_transport);

    (void)std::move(signaler_end).close();
    (void)std::move(waiter_end).close();

    CRUCIBLE_TEST_REQUIRE(observed == signal);
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 6);
}

}  // namespace

int main() {
    using ::crucible::concurrent::VendorBackend;

    std::fprintf(stderr, "[test_chainedge_session]\n");
    run_test("raw_cpu", [] { test_raw_chainedge_backend<VendorBackend::CPU>(); });
    run_test("raw_nv_stub", [] { test_raw_chainedge_backend<VendorBackend::NV>(); });
    run_test("raw_amd_stub", [] { test_raw_chainedge_backend<VendorBackend::AMD>(); });
    run_test("raw_tpu_stub", [] { test_raw_chainedge_backend<VendorBackend::TPU>(); });
    run_test("raw_trn_stub", [] { test_raw_chainedge_backend<VendorBackend::TRN>(); });
    run_test("permissioned_cpu", [] {
        test_permissioned_chainedge_backend<VendorBackend::CPU>();
    });
    run_test("permissioned_nv_stub", [] {
        test_permissioned_chainedge_backend<VendorBackend::NV>();
    });
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
