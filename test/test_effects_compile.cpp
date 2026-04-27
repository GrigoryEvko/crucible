// ═══════════════════════════════════════════════════════════════════
// test_effects_compile — sentinel TU for effects/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every effects/* header through the test target's full
// -Werror matrix.
//
// Coverage: 4 headers (Capabilities, Computation, EffectRow, Effects
// umbrella).  When a new effects/* header ships, add its include below.
// The legacy crucible/Effects.h fx::* tree and the compat/Fx.h shim
// were both deleted in FOUND-B07 / METX-5; the cap::* / Bg / Init /
// Test surface lives in Capabilities.h alongside the Effect enum.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Effects.h>

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

void test_capabilities_compile()  {}
void test_computation_compile()   {}
void test_effect_row_compile()    {}
void test_effects_umbrella()      {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_effects_compile:\n");
    run_test("test_capabilities_compile",  test_capabilities_compile);
    run_test("test_computation_compile",   test_computation_compile);
    run_test("test_effect_row_compile",    test_effect_row_compile);
    run_test("test_effects_umbrella",      test_effects_umbrella);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
