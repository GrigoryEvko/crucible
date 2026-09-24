#include <crucible/safety/InferredRow.h>

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace effects = ::crucible::effects;

}  // namespace

namespace ir_test {

inline void f_nullary() noexcept {}
inline void f_int(int) noexcept {}
inline void f_int_double(int, double) noexcept {}
inline void f_ptr(int*) noexcept {}

inline void f_alloc_only(effects::Alloc) noexcept {}
inline void f_io_only(effects::IO) noexcept {}
inline void f_block_only(effects::Block) noexcept {}

inline void f_bg_only(effects::Bg) noexcept {}
inline void f_init_only(effects::Init) noexcept {}
inline void f_test_only(effects::Test) noexcept {}

inline void f_alloc_size(effects::Alloc, std::size_t) noexcept {}
inline void f_int_alloc_double(int, effects::Alloc, double) noexcept {}

inline void f_alloc_io(effects::Alloc, effects::IO) noexcept {}
inline void f_io_alloc(effects::IO, effects::Alloc) noexcept {}

inline void f_three_caps(effects::Alloc, effects::IO, effects::Block) noexcept {}

inline void f_alloc_alloc(effects::Alloc, effects::Alloc) noexcept {}

inline void f_alloc_ref(effects::Alloc&) noexcept {}
inline void f_alloc_const_ref(effects::Alloc const&) noexcept {}
inline void f_alloc_rvalue_ref(effects::Alloc&&) noexcept {}

inline effects::Alloc f_returns_alloc() noexcept { return {}; }

inline void f_bg_with_alloc(effects::Bg, effects::Alloc) noexcept {}

inline void f_bg_bg(effects::Bg, effects::Bg) noexcept {}

}  // namespace ir_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::inferred_row_smoke_test()); }

void test_pure_function_nullary() {
    static_assert(extract::is_pure_function_v<&ir_test::f_nullary>);
    static_assert(extract::IsPureFunction<&ir_test::f_nullary>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_nullary> == 0);
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_nullary>, effects::EmptyRow>);
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
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_only>, effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_alloc_only, effects::Effect::Alloc>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_alloc_only, effects::Effect::IO>);
}

void test_single_cap_io() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_io_only>, effects::Row<effects::Effect::IO>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_io_only, effects::Effect::IO>);
}

void test_single_cap_block() {
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_block_only>, effects::Row<effects::Effect::Block>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_block_only, effects::Effect::Block>);
}

void test_single_context_bg() {
    static_assert(!extract::is_pure_function_v<&ir_test::f_bg_only>);
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_bg_only>, effects::Row<effects::Effect::Bg>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_only, effects::Effect::Bg>);
    // A context atom does not expand into the atoms it implies.  The row
    // keeps the parameter type as written, and whether to project it is
    // the caller's decision.
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only, effects::Effect::Alloc>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only, effects::Effect::IO>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_bg_only, effects::Effect::Block>);
}

void test_single_context_init() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_init_only>, effects::Row<effects::Effect::Init>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_init_only, effects::Effect::Init>);
}

void test_single_context_test() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_test_only>, effects::Row<effects::Effect::Test>>);
    static_assert(extract::function_has_effect_v<&ir_test::f_test_only, effects::Effect::Test>);
}

void test_cap_with_non_cap_args() {
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_size>, effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_alloc_size> == 1);

    // Classification does not depend on where in the list the parameter
    // sits.
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_int_alloc_double>, effects::Row<effects::Effect::Alloc>>);
}

void test_two_distinct_caps_declaration_order() {
    // Rows are built by inserting each new atom in declaration order.
    // They are never sorted into a canonical form.
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_io>,
                                 effects::Row<effects::Effect::Alloc, effects::Effect::IO>>);
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_io_alloc>,
                                 effects::Row<effects::Effect::IO, effects::Effect::Alloc>>);

    // So the two rows are different types while containing each other.
    static_assert(
        !std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_io>, extract::inferred_row_t<&ir_test::f_io_alloc>>);
    static_assert(effects::is_subrow_v<extract::inferred_row_t<&ir_test::f_alloc_io>,
                                       extract::inferred_row_t<&ir_test::f_io_alloc>>);
    static_assert(effects::is_subrow_v<extract::inferred_row_t<&ir_test::f_io_alloc>,
                                       extract::inferred_row_t<&ir_test::f_alloc_io>>);
}

void test_three_caps() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_three_caps>,
                                 effects::Row<effects::Effect::Alloc, effects::Effect::IO, effects::Effect::Block>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_three_caps> == 3);
}

void test_dedup_repeated_cap() {
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_alloc>, effects::Row<effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_alloc_alloc> == 1);
}

void test_dedup_repeated_context() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_bg_bg>, effects::Row<effects::Effect::Bg>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_bg_bg> == 1);
}

void test_cv_ref_qualified_cap_classifies() {
    // A parameter type keeps the qualifiers it was declared with.  The
    // classifier strips them before it compares, which is why all three
    // forms land on one row.
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_ref>, effects::Row<effects::Effect::Alloc>>);
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_const_ref>, effects::Row<effects::Effect::Alloc>>);
    static_assert(
        std::is_same_v<extract::inferred_row_t<&ir_test::f_alloc_rvalue_ref>, effects::Row<effects::Effect::Alloc>>);
}

void test_return_type_does_not_contribute() {
    // The row states what the callee demands of the caller's context, and
    // that is written in the parameter list.  A capability handed back
    // out is for the return type's own wrapper to interpret.
    static_assert(extract::is_pure_function_v<&ir_test::f_returns_alloc>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_returns_alloc> == 0);
}

void test_context_and_cap_coexist() {
    static_assert(std::is_same_v<extract::inferred_row_t<&ir_test::f_bg_with_alloc>,
                                 effects::Row<effects::Effect::Bg, effects::Effect::Alloc>>);
    static_assert(extract::inferred_row_count_v<&ir_test::f_bg_with_alloc> == 2);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_with_alloc, effects::Effect::Bg>);
    static_assert(extract::function_has_effect_v<&ir_test::f_bg_with_alloc, effects::Effect::Alloc>);
}

void test_subrow_substitution_principle() {
    // A function demanding one row can be called from any context whose
    // row contains it.  That substitution is what the checks below pin.
    using R_alloc = extract::inferred_row_t<&ir_test::f_alloc_only>;
    using R_alloc_io = extract::inferred_row_t<&ir_test::f_alloc_io>;
    using R_three_caps = extract::inferred_row_t<&ir_test::f_three_caps>;
    using R_pure = extract::inferred_row_t<&ir_test::f_nullary>;

    static_assert(effects::is_subrow_v<R_pure, R_alloc>);
    static_assert(effects::is_subrow_v<R_pure, R_three_caps>);
    static_assert(effects::is_subrow_v<R_alloc, R_alloc_io>);
    static_assert(effects::is_subrow_v<R_alloc, R_three_caps>);
    static_assert(effects::is_subrow_v<R_alloc_io, R_three_caps>);
    static_assert(!effects::is_subrow_v<R_alloc_io, R_alloc>);
    static_assert(!effects::is_subrow_v<R_three_caps, R_alloc>);
}

void test_concept_form_in_constraints() {
    auto requires_pure = []<auto FnPtr>()
        requires extract::IsPureFunction<FnPtr>
    { return true; };

    EXPECT_TRUE(requires_pure.template operator()<&ir_test::f_nullary>());
    EXPECT_TRUE(requires_pure.template operator()<&ir_test::f_int>());
    // There is no assertion here that an effectful function is refused.
    // Calling the lambda with one is a hard error rather than a false
    // value, so a negative-compile fixture covers that side instead.
}

void test_membership_query_completeness() {
    // Each query goes through the whole-function predicate rather than a
    // function-local alias for the row, because a local typedef spliced
    // into a template argument reads ambiguously.
    static_assert(extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Alloc>);
    static_assert(extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::IO>);
    static_assert(extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Block>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Bg>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Init>);
    static_assert(!extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Test>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pure = extract::is_pure_function_v<&ir_test::f_nullary>;
    bool baseline_alloc = !extract::is_pure_function_v<&ir_test::f_alloc_only>;
    EXPECT_TRUE(baseline_pure);
    EXPECT_TRUE(baseline_alloc);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pure == extract::is_pure_function_v<&ir_test::f_nullary>);
        EXPECT_TRUE(baseline_alloc == !extract::is_pure_function_v<&ir_test::f_alloc_only>);
        EXPECT_TRUE((extract::function_has_effect_v<&ir_test::f_three_caps, effects::Effect::Block>));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_inferred_row:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_pure_function_nullary", test_pure_function_nullary);
    run_test("test_pure_function_with_non_cap_args", test_pure_function_with_non_cap_args);
    run_test("test_single_cap_alloc", test_single_cap_alloc);
    run_test("test_single_cap_io", test_single_cap_io);
    run_test("test_single_cap_block", test_single_cap_block);
    run_test("test_single_context_bg", test_single_context_bg);
    run_test("test_single_context_init", test_single_context_init);
    run_test("test_single_context_test", test_single_context_test);
    run_test("test_cap_with_non_cap_args", test_cap_with_non_cap_args);
    run_test("test_two_distinct_caps_declaration_order", test_two_distinct_caps_declaration_order);
    run_test("test_three_caps", test_three_caps);
    run_test("test_dedup_repeated_cap", test_dedup_repeated_cap);
    run_test("test_dedup_repeated_context", test_dedup_repeated_context);
    run_test("test_cv_ref_qualified_cap_classifies", test_cv_ref_qualified_cap_classifies);
    run_test("test_return_type_does_not_contribute", test_return_type_does_not_contribute);
    run_test("test_context_and_cap_coexist", test_context_and_cap_coexist);
    run_test("test_subrow_substitution_principle", test_subrow_substitution_principle);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_membership_query_completeness", test_membership_query_completeness);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
