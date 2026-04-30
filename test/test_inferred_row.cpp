// ═══════════════════════════════════════════════════════════════════
// test_inferred_row — sentinel TU for safety/InferredRow.h
//
// FOUND-D10 — extract Met(X) effect rows from function signatures.
// Mirrors test_inferred_permission_tags discipline (D11 sibling) with
// effect-atom classification instead of CSL tag classification.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/InferredRow.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace effects = ::crucible::effects;

}  // namespace

namespace ir_test {

// ── Function signature catalog for D10 audit ─────────────────────────

inline void f_nullary() noexcept {}
inline void f_int(int) noexcept {}
inline void f_int_double(int, double) noexcept {}
inline void f_ptr(int*) noexcept {}

// Single cap-tag parameters.
inline void f_alloc_only(effects::Alloc) noexcept {}
inline void f_io_only(effects::IO) noexcept {}
inline void f_block_only(effects::Block) noexcept {}

// Single context parameters.
inline void f_bg_only(effects::Bg) noexcept {}
inline void f_init_only(effects::Init) noexcept {}
inline void f_test_only(effects::Test) noexcept {}

// Cap-tag with non-cap parameters.
inline void f_alloc_size(effects::Alloc, std::size_t) noexcept {}
inline void f_int_alloc_double(int, effects::Alloc, double) noexcept {}

// Multiple distinct cap-tags — both directions of declaration order.
inline void f_alloc_io(effects::Alloc, effects::IO) noexcept {}
inline void f_io_alloc(effects::IO, effects::Alloc) noexcept {}

// Three-cap parameter chain.
inline void f_three_caps(effects::Alloc, effects::IO, effects::Block) noexcept {}

// Duplicated cap-tag parameter — dedup boundary.
inline void f_alloc_alloc(effects::Alloc, effects::Alloc) noexcept {}

// Cv-ref qualified cap-tag parameters — should still classify.
inline void f_alloc_ref(effects::Alloc&) noexcept {}
inline void f_alloc_const_ref(effects::Alloc const&) noexcept {}
inline void f_alloc_rvalue_ref(effects::Alloc&&) noexcept {}

// Returns a cap-tag — return types do NOT contribute to the row.
inline effects::Alloc f_returns_alloc() noexcept { return {}; }

// Bg context + bare cap parameter — both contribute (Bg does NOT
// auto-expand into its constituent Alloc/IO/Block).
inline void f_bg_with_alloc(effects::Bg, effects::Alloc) noexcept {}

// Repeated context — dedup.
inline void f_bg_bg(effects::Bg, effects::Bg) noexcept {}

}  // namespace ir_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::inferred_row_smoke_test());
}

void test_pure_function_nullary() {
    static_assert(extract::is_pure_function_v<&ir_test::f_nullary>);
    static_assert(extract::IsPureFunction<&ir_test::f_nullary>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_nullary> == 0);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_nullary>,
        effects::EmptyRow>);
}

void test_pure_function_with_non_cap_args() {
    static_assert(extract::is_pure_function_v<&ir_test::f_int>);
    static_assert(extract::is_pure_function_v<&ir_test::f_int_double>);
    static_assert(extract::is_pure_function_v<&ir_test::f_ptr>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_int_double> == 0);
}

void test_single_cap_alloc() {
    static_assert(!extract::is_pure_function_v<&ir_test::f_alloc_only>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_alloc_only> == 1);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_only>,
        effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_alloc_only,
                                                 effects::Effect::Alloc>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_alloc_only,
                                                  effects::Effect::IO>);
}

void test_single_cap_io() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_io_only>,
        effects::Row<effects::Effect::IO>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_io_only,
                                                 effects::Effect::IO>);
}

void test_single_cap_block() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_block_only>,
        effects::Row<effects::Effect::Block>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_block_only,
                                                 effects::Effect::Block>);
}

void test_single_context_bg() {
    static_assert(!extract::is_pure_function_v<&ir_test::f_bg_only>);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_bg_only>,
        effects::Row<effects::Effect::Bg>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_only,
                                                 effects::Effect::Bg>);
    // Bg context does NOT auto-expand into Alloc/IO/Block.  The
    // dispatcher decides whether to lift Bg into its constituent
    // atoms; the inferred row preserves the parameter type as-is.
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only,
                                                  effects::Effect::Alloc>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only,
                                                  effects::Effect::IO>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only,
                                                  effects::Effect::Block>);
}

void test_single_context_init() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_init_only>,
        effects::Row<effects::Effect::Init>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_init_only,
                                                 effects::Effect::Init>);
}

void test_single_context_test() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_test_only>,
        effects::Row<effects::Effect::Test>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_test_only,
                                                 effects::Effect::Test>);
}

void test_cap_with_non_cap_args() {
    // Non-cap parameters do NOT contribute to the row — the row
    // contains exactly one atom (Alloc).
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_size>,
        effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_alloc_size> == 1);

    // Position-independent: cap appearing at parameter index 1 still
    // classifies into the row.
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_int_alloc_double>,
        effects::Row<effects::Effect::Alloc>>);
}

void test_two_distinct_caps_declaration_order() {
    // Declaration order is preserved (insert-unique semantics, NOT
    // canonicalize).
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_io>,
        effects::Row<effects::Effect::Alloc, effects::Effect::IO>>);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_io_alloc>,
        effects::Row<effects::Effect::IO, effects::Effect::Alloc>>);

    // The two rows are STRUCTURALLY distinct but Subrow-equivalent.
    static_assert(!std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_io>,
        extract::inferred_row_t<&ir_test::f_io_alloc>>);
    static_assert(effects::is_subrow_v<
        extract::inferred_row_t<&ir_test::f_alloc_io>,
        extract::inferred_row_t<&ir_test::f_io_alloc>>);
    static_assert(effects::is_subrow_v<
        extract::inferred_row_t<&ir_test::f_io_alloc>,
        extract::inferred_row_t<&ir_test::f_alloc_io>>);
}

void test_three_caps() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_three_caps>,
        effects::Row<effects::Effect::Alloc,
                     effects::Effect::IO,
                     effects::Effect::Block>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_three_caps> == 3);
}

void test_dedup_repeated_cap() {
    // f_alloc_alloc has two effects::Alloc parameters — dedup yields
    // a singleton row.
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_alloc>,
        effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_alloc_alloc> == 1);
}

void test_dedup_repeated_context() {
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_bg_bg>,
        effects::Row<effects::Effect::Bg>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_bg_bg> == 1);
}

void test_cv_ref_qualified_cap_classifies() {
    // SignatureTraits leaves cv-ref on parameter types as-declared
    // (e.g. `int&` stays `int&`); the classifier strips cv-ref via
    // remove_cvref_t before comparing to the canonical cap types.
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_ref>,
        effects::Row<effects::Effect::Alloc>>);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_const_ref>,
        effects::Row<effects::Effect::Alloc>>);
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_alloc_rvalue_ref>,
        effects::Row<effects::Effect::Alloc>>);
}

void test_return_type_does_not_contribute() {
    // Returns effects::Alloc but takes no cap parameters — pure.
    // A cap-tag in the return position is the wrapper's responsibility
    // to interpret (a Linear<DetSafe<Alloc>>-style return), NOT the
    // inferred-row's; the row reflects only the OBLIGATIONS imposed
    // on the caller's context, which are read from the parameter list.
    static_assert(extract::is_pure_function_v<&ir_test::f_returns_alloc>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_returns_alloc> == 0);
}

void test_context_and_cap_coexist() {
    // f_bg_with_alloc takes (Bg, Alloc).  The row carries both atoms
    // distinctly — Bg is NOT auto-expanded.  The dispatcher decides
    // whether to project Bg → {Alloc, IO, Block} via row_union_t.
    static_assert(std::is_same_v<
        extract::inferred_row_t<&ir_test::f_bg_with_alloc>,
        effects::Row<effects::Effect::Bg, effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_bg_with_alloc> == 2);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_with_alloc,
                                                 effects::Effect::Bg>);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_with_alloc,
                                                 effects::Effect::Alloc>);
}

void test_subrow_substitution_principle() {
    // The substitution principle from CLAUDE.md L0: a function
    // requiring row R can be called from a context holding any row
    // R' ⊇ R.  Pin this with explicit Subrow checks.
    using R_alloc      = extract::inferred_row_t<&ir_test::f_alloc_only>;
    using R_alloc_io   = extract::inferred_row_t<&ir_test::f_alloc_io>;
    using R_three_caps = extract::inferred_row_t<&ir_test::f_three_caps>;
    using R_pure       = extract::inferred_row_t<&ir_test::f_nullary>;

    static_assert(effects::is_subrow_v<R_pure, R_alloc>);
    static_assert(effects::is_subrow_v<R_pure, R_three_caps>);
    static_assert(effects::is_subrow_v<R_alloc, R_alloc_io>);
    static_assert(effects::is_subrow_v<R_alloc, R_three_caps>);
    static_assert(effects::is_subrow_v<R_alloc_io, R_three_caps>);
    static_assert(!effects::is_subrow_v<R_alloc_io, R_alloc>);
    static_assert(!effects::is_subrow_v<R_three_caps, R_alloc>);
}

void test_concept_form_in_constraints() {
    // Generic lambda constrained on IsPureFunction — usable as a
    // dispatcher gate that admits only effect-free callees.
    auto requires_pure = []<auto FnPtr>()
        requires extract::IsPureFunction<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(requires_pure.template operator()<&ir_test::f_nullary>());
    EXPECT_TRUE(requires_pure.template operator()<&ir_test::f_int>());
    // Effectful functions reject the gate; verifying via runtime
    // because the failed substitution is a hard error and we cannot
    // express "this concept rejects FnPtr X" with static_assert
    // directly.  The neg-compile fixture closes this side.
}

void test_membership_query_completeness() {
    // For each Effect atom, function_has_effect_v should match the
    // structural row contents.  Use the function_has_effect_v
    // wrapper (which threads the FnPtr through internally) instead
    // of staging a local `using R = ...` — keeps the static_assert
    // expressions simpler and avoids any ambiguity around
    // function-local typedefs spliced into template-argument
    // positions.
    static_assert( extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::Alloc>);
    static_assert( extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::IO>);
    static_assert( extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::Block>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::Bg>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::Init>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps,
                                                  effects::Effect::Test>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pure   = extract::is_pure_function_v<&ir_test::f_nullary>;
    bool baseline_alloc  = !extract::is_pure_function_v<&ir_test::f_alloc_only>;
    EXPECT_TRUE(baseline_pure);
    EXPECT_TRUE(baseline_alloc);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pure ==
                    extract::is_pure_function_v<&ir_test::f_nullary>);
        EXPECT_TRUE(baseline_alloc ==
                    !extract::is_pure_function_v<&ir_test::f_alloc_only>);
        EXPECT_TRUE((extract::function_has_effect_v<&ir_test::f_three_caps,
                                                    effects::Effect::Block>));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_inferred_row:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_pure_function_nullary", test_pure_function_nullary);
    run_test("test_pure_function_with_non_cap_args",
             test_pure_function_with_non_cap_args);
    run_test("test_single_cap_alloc", test_single_cap_alloc);
    run_test("test_single_cap_io", test_single_cap_io);
    run_test("test_single_cap_block", test_single_cap_block);
    run_test("test_single_context_bg", test_single_context_bg);
    run_test("test_single_context_init", test_single_context_init);
    run_test("test_single_context_test", test_single_context_test);
    run_test("test_cap_with_non_cap_args", test_cap_with_non_cap_args);
    run_test("test_two_distinct_caps_declaration_order",
             test_two_distinct_caps_declaration_order);
    run_test("test_three_caps", test_three_caps);
    run_test("test_dedup_repeated_cap", test_dedup_repeated_cap);
    run_test("test_dedup_repeated_context", test_dedup_repeated_context);
    run_test("test_cv_ref_qualified_cap_classifies",
             test_cv_ref_qualified_cap_classifies);
    run_test("test_return_type_does_not_contribute",
             test_return_type_does_not_contribute);
    run_test("test_context_and_cap_coexist",
             test_context_and_cap_coexist);
    run_test("test_subrow_substitution_principle",
             test_subrow_substitution_principle);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_membership_query_completeness",
             test_membership_query_completeness);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
