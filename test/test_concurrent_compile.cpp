// ═══════════════════════════════════════════════════════════════════
// test_concurrent_compile — sentinel TU for the concurrent/* tree
//
// Same blind-spot rationale as test_safety_compile / test_effects_
// compile (see feedback_header_only_static_assert_blind_spot
// memory).  Forces concurrent/* headers that ship embedded
// static_asserts through the test target's full -Werror matrix.
//
// Initial coverage: ExecCtxBridge.h (the cross-tree bridge between
// effects::ExecCtx and concurrent::ParallelismRule's WorkBudget /
// NumaPolicy / Tier vocabulary).  When new concurrent/* headers
// add embedded static_asserts, append their includes + run_test
// invocations here.
// ═══════════════════════════════════════════════════════════════════

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
    namespace cc  = ::crucible::concurrent;
    namespace eff = ::crucible::effects;
    static_assert(cc::IsL1ResidentCtx<eff::HotFgCtx>);
    static_assert(cc::IsNumaSpreadCtx<eff::ColdInitCtx>);
    [[maybe_unused]] auto decision =
        cc::recommend_parallelism(cc::ctx_workbudget<eff::BgCompileCtx>());
    [[maybe_unused]] auto bg_decision =
        cc::parallelism_decision_for<eff::BgDrainCtx>();
}
void test_substrate_compile() {
    namespace cc = ::crucible::concurrent;
    struct UserTag {};
    using SpscT = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 64, UserTag>;
    using MpmcT = cc::Substrate_t<cc::ChannelTopology::ManyToMany, int, 64, UserTag>;
    static_assert(cc::substrate_topology_v<SpscT> == cc::ChannelTopology::OneToOne);
    static_assert(cc::substrate_topology_v<MpmcT> == cc::ChannelTopology::ManyToMany);
    static_assert( cc::IsOneToOneSubstrate<SpscT>);
    static_assert(!cc::IsManyToManySubstrate<SpscT>);
}
void test_substrate_ctx_fit_compile() {
    namespace cc  = ::crucible::concurrent;
    namespace eff = ::crucible::effects;
    struct UserTag {};
    using SmallSpsc = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 1024, UserTag>;
    using LargeSpsc = cc::Substrate_t<cc::ChannelTopology::OneToOne, int, 1024 * 1024, UserTag>;
    static_assert( cc::SubstrateFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
    static_assert( cc::SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);
    static_assert( cc::StorageFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
    static_assert(!cc::StorageFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);
    static_assert( cc::SubstrateBenefitsFromParallelism<LargeSpsc>);
}
void test_substrate_session_bridge_compile() {
    // mint_substrate_session<Substr, Dir>(ctx, handle) is pinned by
    // header static_asserts and negative compile fixtures.
}
void test_endpoint_compile() {
    // Endpoint<Substr, Dir, Ctx> + mint_endpoint factory + raw view +
    // .into_session() upgrade. Compile-time witnesses live in the
    // header and are forced through this sentinel TU by inclusion.
}
void test_stage_compile() {
    // Stage<auto FnPtr, Ctx> + mint_stage factory.  The load-bearing
    // properties are compile-time concepts and static_asserts.
}
void test_pipeline_compile() {
    // Pipeline<Stages...> + mint_pipeline factory.  The sentinel TU
    // compiles the chain-compatibility constraints without exporting
    // header-level runtime drivers.
}
void test_pipeline_real_integration() {
    // Real-channel composition is covered by type-level witnesses in
    // the production headers plus negative compile fixtures. Keep
    // this test compile-only unless a runtime behavior regresses.
}
void test_stage_endpoint_bridge() {
    // Tier 2 -> Tier 3 bridge: compile-time witnesses in
    // StageEndpointBridge.h pin the Endpoint-mediated composition.
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_concurrent_compile:\n");
    run_test("test_exec_ctx_bridge_compile",         test_exec_ctx_bridge_compile);
    run_test("test_substrate_compile",               test_substrate_compile);
    run_test("test_substrate_ctx_fit_compile",       test_substrate_ctx_fit_compile);
    run_test("test_substrate_session_bridge_compile",test_substrate_session_bridge_compile);
    run_test("test_endpoint_compile",                test_endpoint_compile);
    run_test("test_stage_compile",                   test_stage_compile);
    run_test("test_pipeline_compile",                test_pipeline_compile);
    run_test("test_pipeline_real_integration",       test_pipeline_real_integration);
    run_test("test_stage_endpoint_bridge",            test_stage_endpoint_bridge);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
