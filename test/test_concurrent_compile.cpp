// A header-only static_assert block is only checked where a translation
// unit pulls the header in. This file pulls in the concurrent headers that
// ship such blocks so each runs under the test target's full warning
// matrix. Most probe bodies are empty on purpose: including the header is
// the test. When a new concurrent header adds an embedded block, add its
// include and a probe here.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/ExecCtxBridge.h>
#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct TestFailure {};
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

void test_exec_ctx_bridge_compile() {
    namespace cc = ::crucible::concurrent;
    namespace eff = ::crucible::effects;
    static_assert(cc::IsL1ResidentCtx<eff::HotFgCtx>);
    static_assert(cc::IsNumaSpreadCtx<eff::ColdInitCtx>);
    [[maybe_unused]] auto decision = cc::recommend_parallelism(cc::ctx_workbudget<eff::BgCompileCtx>());
    [[maybe_unused]] auto bg_decision = cc::parallelism_decision_for<eff::BgDrainCtx>();
}
void test_substrate_compile() {
    namespace cc = ::crucible::concurrent;
    struct UserTag {};
    using SpscT = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 64, UserTag>;
    using MpmcT = cc::Substrate_t<cc::ChannelTopology::ManyToMany, int, 64, UserTag>;
    static_assert(cc::substrate_topology_v<SpscT> == cc::ChannelTopology::OneToOne);
    static_assert(cc::substrate_topology_v<MpmcT> == cc::ChannelTopology::ManyToMany);
    static_assert(cc::IsOneToOneSubstrate<SpscT>);
    static_assert(!cc::IsManyToManySubstrate<SpscT>);
}
void test_substrate_ctx_fit_compile() {
    namespace cc = ::crucible::concurrent;
    namespace eff = ::crucible::effects;
    struct UserTag {};
    using SmallSpsc = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 1024, UserTag>;
    using LargeSpsc = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 1024 * 1024, UserTag>;
    static_assert(cc::SubstrateFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
    static_assert(cc::SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);
    static_assert(cc::StorageFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
    static_assert(!cc::StorageFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);
    static_assert(cc::SubstrateBenefitsFromParallelism<LargeSpsc>);
}
void test_substrate_session_bridge_compile() {}
void test_endpoint_compile() {}
void test_stage_compile() {}
void test_pipeline_compile() {}
void test_pipeline_real_integration() {}
void test_stage_endpoint_bridge() {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_concurrent_compile:\n");
    run_test("test_exec_ctx_bridge_compile", test_exec_ctx_bridge_compile);
    run_test("test_substrate_compile", test_substrate_compile);
    run_test("test_substrate_ctx_fit_compile", test_substrate_ctx_fit_compile);
    run_test("test_substrate_session_bridge_compile", test_substrate_session_bridge_compile);
    run_test("test_endpoint_compile", test_endpoint_compile);
    run_test("test_stage_compile", test_stage_compile);
    run_test("test_pipeline_compile", test_pipeline_compile);
    run_test("test_pipeline_real_integration", test_pipeline_real_integration);
    run_test("test_stage_endpoint_bridge", test_stage_endpoint_bridge);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
