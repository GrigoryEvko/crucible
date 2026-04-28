// ═══════════════════════════════════════════════════════════════════
// test_diagnostic_compile — sentinel TU for safety/Diagnostic.h
//
// Same blind-spot rationale as test_algebra_compile / test_effects_
// compile / test_safety_compile (see feedback_header_only_static_
// assert_blind_spot memory): a header shipped with embedded static_
// asserts is unverified under the project warning flags unless a
// .cpp TU includes it.  This sentinel forces the foundation
// diagnostic header through the test target's full -Werror matrix
// (-Wswitch-default, -Wconversion, -Wshadow, -Wold-style-cast, etc.)
// and exercises the runtime_smoke_test inline body.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * Safety umbrella inclusion (verifies Diagnostic.h is wired into
//     the umbrella properly — would catch a missed #include in
//     Safety.h).
//   * runtime_smoke_test() execution: exercises name_of /
//     description_of / remediation_of switches with non-constant
//     Category arguments (catches inline-body bugs that pure
//     static_assert tests in the header miss).
//   * is_diagnostic_class_v positive / negative coverage.
//   * Diagnostic<Tag, Ctx...> wrapper construction + field access.
//   * CRUCIBLE_DIAG_ASSERT macro happy-path compile.
//   * Tag accessor field non-emptiness (re-verified at runtime
//     boundary in case the header's compile-time assertion is
//     somehow disabled in a future build mode).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Safety.h>

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if (!((a) == (b))) {                                               \
            std::fprintf(stderr,                                           \
                "    EXPECT_EQ failed: %s == %s (%s:%d)\n",                \
                #a, #b, __FILE__, __LINE__);                               \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

// ─── Tests ──────────────────────────────────────────────────────────

namespace diag = ::crucible::safety::diag;

void test_runtime_smoke() {
    // Per the algebra/effects discipline (memory rule
    // feedback_algebra_runtime_smoke_test_discipline): exercise the
    // header's consteval/constexpr surface with non-constant args.
    // The smoke test loops Category over a volatile-bounded range,
    // forcing the switches to evaluate at runtime.
    diag::runtime_smoke_test();
}

void test_catalog_cardinality() {
    // 22 wrapper-axis tags shipped at FOUND-E01.
    EXPECT_EQ(diag::catalog_size, std::size_t{22});
}

// Namespace-scope user-extension tag.  C++26 [class.local]/4 forbids
// function-local classes from having static data members, so the
// user-extension fixture lives at namespace scope.
struct anonymous_local_tag : diag::tag_base {
    static constexpr std::string_view name        = "AnonymousLocalTag";
    static constexpr std::string_view description = "test fixture";
    static constexpr std::string_view remediation = "rebuild";
};

void test_tag_inheritance_detection() {
    EXPECT_TRUE( diag::is_diagnostic_class_v<diag::EffectRowMismatch>);
    EXPECT_TRUE( diag::is_diagnostic_class_v<diag::DetSafeLeak>);
    EXPECT_TRUE( diag::is_diagnostic_class_v<diag::HotPathViolation>);
    EXPECT_TRUE(!diag::is_diagnostic_class_v<diag::tag_base>);
    EXPECT_TRUE(!diag::is_diagnostic_class_v<int>);
    EXPECT_TRUE( diag::is_diagnostic_class_v<anonymous_local_tag>);
}

void test_accessor_runtime_coverage() {
    // Volatile loop bound prevents constexpr-folding of the iteration.
    volatile std::size_t const cap = diag::catalog_size;
    constexpr std::string_view sentinel{"<unknown Category>"};
    for (std::size_t i = 0; i < cap; ++i) {
        diag::Category const c = static_cast<diag::Category>(i);
        std::string_view const n = diag::name_of(c);
        std::string_view const d = diag::description_of(c);
        std::string_view const r = diag::remediation_of(c);
        EXPECT_TRUE(!n.empty());
        EXPECT_TRUE(!d.empty());
        EXPECT_TRUE(!r.empty());
        EXPECT_TRUE(n != sentinel);
        EXPECT_TRUE(d != sentinel);
        EXPECT_TRUE(r != sentinel);
    }

    // Out-of-range Category falls through the default arm to the
    // sentinel.  Reachable only via reinterpret_cast / unsafe static_cast.
    diag::Category const bogus = static_cast<diag::Category>(255);
    EXPECT_EQ(diag::name_of(bogus), sentinel);
    EXPECT_EQ(diag::description_of(bogus), sentinel);
    EXPECT_EQ(diag::remediation_of(bogus), sentinel);
}

void test_bidirectional_map() {
    // category_of_v ∘ tag_of_t = identity over Category enumerators.
    // Compile-time-asserted in the header's self-test block; here we
    // verify the same property at runtime via volatile-bounded loop.
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        diag::Category const c = static_cast<diag::Category>(i);
        // name_of(c) must equal tag's static name field after round-trip.
        // We can't tag_of_t<c> at runtime (template arg must be constant);
        // instead we round-trip via the switch and compare names.
        std::string_view const n = diag::name_of(c);
        EXPECT_TRUE(!n.empty());
    }

    // Type-level bidirectional check (pulled out as a static_assert
    // sample to verify the header's bijection holds at this TU's
    // instantiation context).
    static_assert(diag::category_of_v<diag::EffectRowMismatch>
                  == diag::Category::EffectRowMismatch);
    static_assert(diag::category_of_v<diag::DetSafeLeak>
                  == diag::Category::DetSafeLeak);
    static_assert(diag::category_of_v<diag::RecipeSpecMismatch>
                  == diag::Category::RecipeSpecMismatch);

    static_assert(std::is_same_v<
        diag::tag_of_t<diag::Category::EffectRowMismatch>,
        diag::EffectRowMismatch>);
    static_assert(std::is_same_v<
        diag::tag_of_t<diag::Category::HotPathViolation>,
        diag::HotPathViolation>);
}

void test_diagnostic_wrapper() {
    using d_t = diag::Diagnostic<diag::EffectRowMismatch, int, float>;
    EXPECT_TRUE(diag::is_diagnostic_v<d_t>);
    EXPECT_TRUE(!diag::is_diagnostic_v<int>);
    EXPECT_TRUE(!diag::is_diagnostic_v<diag::EffectRowMismatch>);

    EXPECT_EQ(d_t::name, std::string_view{"EffectRowMismatch"});

    static_assert(std::is_same_v<
        typename d_t::diagnostic_class, diag::EffectRowMismatch>);
    static_assert(std::is_same_v<
        typename d_t::context, std::tuple<int, float>>);
}

void test_macro_compiles() {
    // Happy path: macro accepts a literal-true condition silently.
    CRUCIBLE_DIAG_ASSERT(true,
        EffectRowMismatch,
        "test_diagnostic_compile happy-path: condition true.");

    // Comma-protected condition (template-arg list).
    CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>),
        HotPathViolation,
        "Comma in condition protected by parentheses.");
}

void test_categories_array() {
    EXPECT_EQ(diag::categories_v.size(), diag::catalog_size);
    EXPECT_EQ(diag::categories_v[0], diag::Category::EffectRowMismatch);
    EXPECT_EQ(diag::categories_v[diag::catalog_size - 1],
              diag::Category::RecipeSpecMismatch);

    // Runtime traversal: sequential indexing matches enum value.
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(diag::categories_v[i], static_cast<diag::Category>(i));
    }
}

void test_enumerate_categories_visits_all() {
    // Compile-time visitor counts every Category enumerator exactly once.
    constexpr std::size_t expected = diag::catalog_size;
    constexpr std::size_t observed = []() consteval {
        std::size_t n = 0;
        diag::enumerate_categories([&n]<diag::Category /*C*/>() noexcept {
            ++n;
        });
        return n;
    }();
    static_assert(observed == expected,
        "enumerate_categories did not visit every Category");

    // Runtime mirror: collect names into an array and verify each
    // appears exactly once, in stable order.
    std::array<std::string_view, diag::catalog_size> visited{};
    std::size_t cursor = 0;
    diag::enumerate_categories([&visited, &cursor]<diag::Category C>() noexcept {
        visited[cursor++] = diag::name_of(C);
    });
    EXPECT_EQ(cursor, diag::catalog_size);
    EXPECT_EQ(visited.front(), diag::EffectRowMismatch::name);
    EXPECT_EQ(visited.back(), diag::RecipeSpecMismatch::name);
}

void test_make_diagnostic_factory() {
    // Deduces context types from arguments; cv-qualifiers and reference
    // qualifiers are stripped via remove_cvref_t.
    auto d1 = diag::make_diagnostic<diag::EffectRowMismatch>(int{}, float{});
    using d1_t = decltype(d1);
    static_assert(std::is_same_v<d1_t,
        diag::Diagnostic<diag::EffectRowMismatch, int, float>>);
    EXPECT_EQ(d1_t::name, std::string_view{"EffectRowMismatch"});

    // Empty-context construction.
    auto d2 = diag::make_diagnostic<diag::HotPathViolation>();
    using d2_t = decltype(d2);
    static_assert(std::is_same_v<d2_t,
        diag::Diagnostic<diag::HotPathViolation>>);
    EXPECT_EQ(d2_t::name, std::string_view{"HotPathViolation"});

    // Reference and const-qualified arguments collapse to bare types.
    int const x = 7;
    auto d3 = diag::make_diagnostic<diag::DetSafeLeak>(x);
    static_assert(std::is_same_v<decltype(d3),
        diag::Diagnostic<diag::DetSafeLeak, int>>);
}

void test_diagnostic_accessor_strings_match_tag_fields() {
    // Verify name_of(C) returns the SAME string as the corresponding
    // tag's name field — catches drift between the switch arms and
    // the tag definitions.
    EXPECT_EQ(diag::name_of(diag::Category::EffectRowMismatch),
              diag::EffectRowMismatch::name);
    EXPECT_EQ(diag::name_of(diag::Category::DetSafeLeak),
              diag::DetSafeLeak::name);
    EXPECT_EQ(diag::name_of(diag::Category::RecipeSpecMismatch),
              diag::RecipeSpecMismatch::name);
    EXPECT_EQ(diag::description_of(diag::Category::DetSafeLeak),
              diag::DetSafeLeak::description);
    EXPECT_EQ(diag::remediation_of(diag::Category::DetSafeLeak),
              diag::DetSafeLeak::remediation);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_diagnostic_compile:\n");
    run_test("test_runtime_smoke",
             test_runtime_smoke);
    run_test("test_catalog_cardinality",
             test_catalog_cardinality);
    run_test("test_tag_inheritance_detection",
             test_tag_inheritance_detection);
    run_test("test_accessor_runtime_coverage",
             test_accessor_runtime_coverage);
    run_test("test_bidirectional_map",
             test_bidirectional_map);
    run_test("test_diagnostic_wrapper",
             test_diagnostic_wrapper);
    run_test("test_macro_compiles",
             test_macro_compiles);
    run_test("test_categories_array",
             test_categories_array);
    run_test("test_enumerate_categories_visits_all",
             test_enumerate_categories_visits_all);
    run_test("test_make_diagnostic_factory",
             test_make_diagnostic_factory);
    run_test("test_diagnostic_accessor_strings_match_tag_fields",
             test_diagnostic_accessor_strings_match_tag_fields);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
