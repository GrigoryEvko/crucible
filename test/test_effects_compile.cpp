// A header that ships its own static_asserts is never checked against the
// project's warning flags until some translation unit includes it. This one
// exists to be that translation unit for the whole effects tree, so a new
// header there belongs in the include list below.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/Capability.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/CtxWrapperLift.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>
#include <crucible/effects/EffectRowProjection.h>
#include <crucible/effects/Effects.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/effects/OsUniverse.h>
#include <crucible/effects/Resources.h>

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

void test_capabilities_compile() {}
void test_computation_compile() {}
void test_effect_row_compile() {}
void test_effects_umbrella() {
    // Neither of these two surfaces is reached by a direct include anywhere
    // in this file. Both arrive through the umbrella alone, so dropping one
    // of its includes fires these assertions rather than going unnoticed.
    namespace ce = ::crucible::effects;
    static_assert(ce::resource_kind_count == 23, "the effects umbrella must reach the resource-kind catalog, which "
                                                 "carries 23 axes.");
    static_assert(std::is_same_v<ce::EmptyConcurrentRow, ce::ConcurrentRow<>>,
                  "the effects umbrella must reach the concurrent-row surface.");
}
void test_fx_aliases_compile() {
    // The header's smoke body is what instantiates its constant-evaluated
    // accessors outside constant evaluation.
    ::crucible::effects::runtime_smoke_test();
}
void test_effect_row_lattice_compile() {
    // Every accessor and bridge of the lattice surface, with non-constant
    // arguments.
    ::crucible::effects::runtime_smoke_test_lattice();
}
void test_os_universe_compile() {
    // Every accessor of the universe descriptor, with non-constant
    // arguments.
    ::crucible::effects::runtime_smoke_test_os_universe();
}
void test_computation_graded_compile() {
    // Every graded accessor reachable through the alias, plus the
    // concept-based capability gates, with non-constant arguments.
    ::crucible::effects::runtime_smoke_test_computation_graded();
}
void test_exec_ctx_compile() {
    // No smoke call here. The context carrier's constraints are entirely at
    // the type level, so there is no runtime behaviour to drive.
    namespace ce = ::crucible::effects;
    constexpr auto bg = ce::ExecCtx<>{}
                            .with_cap<ce::Bg>()
                            .pinned_to<ce::ctx_numa::Local>()
                            .with_alloc<ce::ctx_alloc::Arena>()
                            .with_residency<ce::ctx_resid::L2>()
                            .with_heat<ce::ctx_heat::Warm>()
                            .in_row<ce::Row<ce::Effect::Bg, ce::Effect::Alloc>>();
    static_assert(std::is_same_v<typename decltype(bg)::cap_type, ce::Bg>);
    static_assert(std::is_same_v<typename decltype(bg)::row_type, ce::Row<ce::Effect::Bg, ce::Effect::Alloc>>);
    static_assert(std::is_same_v<typename ce::BgDrainCtx::cap_type, ce::Bg>);
    static_assert(!std::is_same_v<typename ce::HotFgCtx::cap_type, ce::Bg>);
}
void test_capability_compile() {
    // Minting from each authorized pair, then move, consume, and the
    // extractors, with non-constant arguments.
    ::crucible::effects::runtime_smoke_test_capability();
}
void test_ctx_wrapper_lift_compile() {
    // The context-to-wrapper lifts, constructed against the canonical
    // contexts with non-constant arguments.
    ::crucible::effects::runtime_smoke_test_ctx_wrapper_lift();
}
void test_effect_row_projection_compile() {
    // The row-to-bits projection bridge in both directions, with
    // non-constant arguments.
    ::crucible::effects::detail::effect_row_projection_self_test::runtime_smoke_test();
}

// The production Effect enum has no duplicate underlying value, so the
// assertion that checks for one passes on every build and would go on passing
// if the check itself were broken. The enum below deliberately carries a
// duplicate, so the same reflection fold has a case it must reject.
enum class DuplicateValueEnum : std::uint8_t {
    Alpha = 0,
    Beta = 1,
    Gamma = 2,
    Aliased = 0,  // deliberately duplicates Alpha
};

[[nodiscard]] consteval bool duplicate_value_enum_distinct_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DuplicateValueEnum));
    using U = std::underlying_type_t<DuplicateValueEnum>;
    std::uint64_t seen = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto u = static_cast<U>([:en:]);
        if constexpr (static_cast<unsigned>(u) >= 64u) {
            return false;
        } else {
            const std::uint64_t bit = std::uint64_t{1} << static_cast<unsigned>(u);
            if (seen & bit) return false;
            seen |= bit;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(!duplicate_value_enum_distinct_(), "the duplicate-value detection must report false for an enum whose "
                                                 "two enumerators share an underlying value. If this assertion fails, "
                                                 "the production check is broken too, because both use the same "
                                                 "reflection fold.");

void test_effect_underlying_distinct_compile() {
    // The other polarity of the same gate. Together with the assertion
    // above, both answers of the check are witnessed.
    static_assert(::crucible::effects::detail::every_effect_underlying_distinct_(),
                  "the production Effect enum must have distinct "
                  "underlying values.");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_effects_compile:\n");
    run_test("test_capabilities_compile", test_capabilities_compile);
    run_test("test_computation_compile", test_computation_compile);
    run_test("test_effect_row_compile", test_effect_row_compile);
    run_test("test_effects_umbrella", test_effects_umbrella);
    run_test("test_fx_aliases_compile", test_fx_aliases_compile);
    run_test("test_effect_row_lattice_compile", test_effect_row_lattice_compile);
    run_test("test_os_universe_compile", test_os_universe_compile);
    run_test("test_computation_graded_compile", test_computation_graded_compile);
    run_test("test_exec_ctx_compile", test_exec_ctx_compile);
    run_test("test_capability_compile", test_capability_compile);
    run_test("test_ctx_wrapper_lift_compile", test_ctx_wrapper_lift_compile);
    run_test("test_effect_row_projection_compile", test_effect_row_projection_compile);
    run_test("test_effect_underlying_distinct_compile", test_effect_underlying_distinct_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
