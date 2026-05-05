// ═══════════════════════════════════════════════════════════════════
// test_effects_compile — sentinel TU for effects/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every effects/* header through the test target's full
// -Werror matrix.
//
// Coverage: 5 headers (Capabilities, Computation, EffectRow, Effects
// umbrella, FxAliases).  When a new effects/* header ships, add its
// include below.  The legacy crucible/Effects.h fx::* tree and the
// compat/Fx.h shim were both deleted in FOUND-B07 / METX-5; the
// cap::* / Bg / Init / Test surface lives in Capabilities.h alongside
// the Effect enum.  FxAliases.h ships F*-style PureRow / DivRow / STRow
// / AllRow named aliases + IsPure / IsDiv / IsST / IsAll concepts
// (FOUND-G79 / G80).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/Capability.h>
#include <crucible/effects/CtxWrapperLift.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>
#include <crucible/effects/EffectRowProjection.h>
#include <crucible/effects/Effects.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/effects/OsUniverse.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

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
void test_fx_aliases_compile()    {
    // Drive the runtime smoke test so the consteval/constexpr accessor
    // surfaces actually get instantiated under the test target's full
    // -Werror matrix (per feedback_algebra_runtime_smoke_test_discipline).
    ::crucible::effects::runtime_smoke_test();
}
void test_effect_row_lattice_compile() {
    // Same discipline for the H01 lattice surface — drive every
    // accessor + bridge through a non-constant-args path.
    ::crucible::effects::runtime_smoke_test_lattice();
}
void test_os_universe_compile() {
    // Same discipline for the H02 OsUniverse descriptor — drive
    // every Universe accessor (name / atom_name / bit_position /
    // lattice typedef) through a non-constant-args path.
    ::crucible::effects::runtime_smoke_test_os_universe();
}
void test_computation_graded_compile() {
    // Same discipline for the H03 ComputationGraded alias — drive
    // every Graded accessor reachable through the alias (peek /
    // consume / grade / peek_mut / swap / weaken / compose) plus
    // the concept-based capability gates through a non-constant-
    // args path.
    ::crucible::effects::runtime_smoke_test_computation_graded();
}
void test_exec_ctx_compile() {
    // ExecCtx<...> universal context carrier: keep this as a compile
    // sentinel. The production header carries type-level constraints;
    // runtime smoke belongs in test code only when a runtime behavior
    // is actually under test.
    namespace ce = ::crucible::effects;
    constexpr auto bg = ce::ExecCtx<>{}
        .with_cap<ce::Bg>()
        .pinned_to<ce::ctx_numa::Local>()
        .with_alloc<ce::ctx_alloc::Arena>()
        .with_residency<ce::ctx_resid::L2>()
        .with_heat<ce::ctx_heat::Warm>()
        .in_row<ce::Row<ce::Effect::Bg, ce::Effect::Alloc>>();
    static_assert(std::is_same_v<typename decltype(bg)::cap_type, ce::Bg>);
    static_assert(std::is_same_v<typename decltype(bg)::row_type,
                                  ce::Row<ce::Effect::Bg, ce::Effect::Alloc>>);
    static_assert(std::is_same_v<typename ce::BgDrainCtx::cap_type, ce::Bg>);
    static_assert(!std::is_same_v<typename ce::HotFgCtx::cap_type, ce::Bg>);
}
void test_capability_compile() {
    // Capability<E, S> linear cap proof tokens: drive mint_cap from
    // each authorized (Effect, Source) pair, exercise move + consume,
    // verify recognition / extractors / discrimination at runtime.
    ::crucible::effects::runtime_smoke_test_capability();
}
void test_ctx_wrapper_lift_compile() {
    // HotPathFromCtx / AllocClassFromCtx / ResidencyHeatFromCtx —
    // verify the lifts construct cleanly at runtime against canonical
    // contexts.
    ::crucible::effects::runtime_smoke_test_ctx_wrapper_lift();
}
void test_effect_row_projection_compile() {
    // bits_from_row<R> / bits_for<Es...> / row_subsumes_bits /
    // bits_subsumes_row — drive the Row→Bits<Effect> projection
    // bridge through a non-constant args path so the consteval
    // surfaces actually instantiate under the test target's full
    // -Werror matrix.
    ::crucible::effects::detail::effect_row_projection_self_test::
        runtime_smoke_test();
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_effects_compile:\n");
    run_test("test_capabilities_compile",  test_capabilities_compile);
    run_test("test_computation_compile",   test_computation_compile);
    run_test("test_effect_row_compile",    test_effect_row_compile);
    run_test("test_effects_umbrella",      test_effects_umbrella);
    run_test("test_fx_aliases_compile",    test_fx_aliases_compile);
    run_test("test_effect_row_lattice_compile",
             test_effect_row_lattice_compile);
    run_test("test_os_universe_compile",   test_os_universe_compile);
    run_test("test_computation_graded_compile",
             test_computation_graded_compile);
    run_test("test_exec_ctx_compile",      test_exec_ctx_compile);
    run_test("test_capability_compile",    test_capability_compile);
    run_test("test_ctx_wrapper_lift_compile", test_ctx_wrapper_lift_compile);
    run_test("test_effect_row_projection_compile",
             test_effect_row_projection_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
