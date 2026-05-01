// ═══════════════════════════════════════════════════════════════════
// test_concurrent_compile — sentinel TU for the concurrent/* tree
//
// Same blind-spot rationale as test_safety_compile / test_effects_
// compile (see feedback_header_only_static_assert_blind_spot
// memory).  Forces concurrent/* headers that ship embedded
// static_asserts AND/or runtime_smoke_test functions through the
// test target's full -Werror matrix.
//
// Initial coverage: ExecCtxBridge.h (the cross-tree bridge between
// effects::ExecCtx and concurrent::ParallelismRule's WorkBudget /
// NumaPolicy / Tier vocabulary).  When new concurrent/* headers
// add embedded static_asserts or runtime smoke surfaces, append
// their includes + run_test invocations here.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/ExecCtxBridge.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>

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
    // Drive the consteval Ctx-driven extractors through a non-
    // constant-args runtime function (per the algebra/effects
    // runtime-smoke-test discipline).  The bridges resolve at
    // template instantiation; this test confirms the resulting
    // values are usable as runtime arguments to recommend_parallelism.
    ::crucible::concurrent::runtime_smoke_test_exec_ctx_bridge();
}
void test_substrate_compile() {
    // Substrate<Topology, T, Cap, UserTag> metafunction: drive every
    // topology pattern through the static_assert chain and verify
    // the runtime smoke instantiates each substrate type.
    ::crucible::concurrent::runtime_smoke_test_substrate();
}
void test_substrate_ctx_fit_compile() {
    // SubstrateFitsCtxResidency cross-tree composition: drive the
    // residency-fit check + required-tier inverse on canonical
    // (Substrate, Ctx) pairs.
    ::crucible::concurrent::runtime_smoke_test_substrate_ctx_fit();
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_concurrent_compile:\n");
    run_test("test_exec_ctx_bridge_compile",  test_exec_ctx_bridge_compile);
    run_test("test_substrate_compile",        test_substrate_compile);
    run_test("test_substrate_ctx_fit_compile", test_substrate_ctx_fit_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
