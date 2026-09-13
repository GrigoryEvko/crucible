// A static_assert that lives only in a header is never evaluated under
// the project warning flags until some translation unit includes it.
// This one includes the diagnostic header both directly and through the
// umbrella, so a header that fell out of the umbrella would show up as
// an unresolved name here.

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

#define EXPECT_EQ(a, b)                                                                                   \
    do {                                                                                                  \
        if (!((a) == (b))) {                                                                              \
            std::fprintf(stderr, "    EXPECT_EQ failed: %s == %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
            throw TestFailure{};                                                                          \
        }                                                                                                 \
    } while (0)

namespace diag = ::crucible::safety::diag;

// The header's own checks are all constant-evaluated.  This one runs
// the same accessors with non-constant arguments, which is where an
// inline-body defect in a switch would surface.
void test_runtime_smoke() { diag::runtime_smoke_test(); }

// The bound is a floor, not an equality.  The catalog is append-only,
// so this test guards against it shrinking, while the exact size is
// pinned next to the catalog itself.  Writing an equality here would
// redden on every append for no reason.
void test_catalog_cardinality() { EXPECT_TRUE(diag::catalog_size >= std::size_t{30}); }

// The fixture lives at namespace scope because a function-local class
// may not have static data members, and a tag is all static members.
struct anonymous_local_tag : diag::tag_base {
    static constexpr std::string_view name = "AnonymousLocalTag";
    static constexpr std::string_view description = "test fixture";
    static constexpr std::string_view remediation = "rebuild";
};

void test_tag_inheritance_detection() {
    EXPECT_TRUE(diag::is_diagnostic_class_v<diag::EffectRowMismatch>);
    EXPECT_TRUE(diag::is_diagnostic_class_v<diag::DetSafeLeak>);
    EXPECT_TRUE(diag::is_diagnostic_class_v<diag::HotPathViolation>);
    EXPECT_TRUE(!diag::is_diagnostic_class_v<diag::tag_base>);
    EXPECT_TRUE(!diag::is_diagnostic_class_v<int>);
    EXPECT_TRUE(diag::is_diagnostic_class_v<anonymous_local_tag>);
}

void test_accessor_runtime_coverage() {
    // The volatile bound stops the loop folding away, so each accessor
    // is entered with a value the compiler cannot see through.
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

    // A value outside the enumeration reaches the default arm and comes
    // back as the sentinel.  Only a cast can produce such a value, which
    // is why the test has to make one by hand.
    diag::Category const bogus = static_cast<diag::Category>(255);
    EXPECT_EQ(diag::name_of(bogus), sentinel);
    EXPECT_EQ(diag::description_of(bogus), sentinel);
    EXPECT_EQ(diag::remediation_of(bogus), sentinel);
}

void test_bidirectional_map() {
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        diag::Category const c = static_cast<diag::Category>(i);
        // The type-level half of the round trip cannot run here: the
        // tag lookup takes its category as a template argument, and
        // this one is a runtime value.  Only the accessor side can be
        // walked, so the loop checks that every category names
        // something.
        std::string_view const n = diag::name_of(c);
        EXPECT_TRUE(!n.empty());
    }

    static_assert(diag::category_of_v<diag::EffectRowMismatch> == diag::Category::EffectRowMismatch);
    static_assert(diag::category_of_v<diag::DetSafeLeak> == diag::Category::DetSafeLeak);
    static_assert(diag::category_of_v<diag::RecipeSpecMismatch> == diag::Category::RecipeSpecMismatch);

    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::EffectRowMismatch>, diag::EffectRowMismatch>);
    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::HotPathViolation>, diag::HotPathViolation>);
}

void test_diagnostic_wrapper() {
    using d_t = diag::Diagnostic<diag::EffectRowMismatch, int, float>;
    EXPECT_TRUE(diag::is_diagnostic_v<d_t>);
    EXPECT_TRUE(!diag::is_diagnostic_v<int>);
    EXPECT_TRUE(!diag::is_diagnostic_v<diag::EffectRowMismatch>);

    EXPECT_EQ(d_t::name, std::string_view{"EffectRowMismatch"});

    static_assert(std::is_same_v<typename d_t::diagnostic_class, diag::EffectRowMismatch>);
    static_assert(std::is_same_v<typename d_t::context, std::tuple<int, float>>);
}

void test_macro_compiles() {
    CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch, "test_diagnostic_compile happy-path: condition true.");

    // The parentheses keep the comma inside the template argument list
    // from splitting the macro's arguments.
    CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>), HotPathViolation, "Comma in condition protected by parentheses.");
}

// The first and last tags are read out of the catalog rather than named
// here.  Naming one would make every append to the catalog redden this
// file, which tells the appender nothing about what actually broke.
using TrailingTag = std::tuple_element_t<diag::catalog_size - 1, diag::Catalog>;
using LeadingTag = std::tuple_element_t<0, diag::Catalog>;

void test_categories_array() {
    EXPECT_EQ(diag::categories_v.size(), diag::catalog_size);
    EXPECT_EQ(diag::categories_v[0], diag::category_of_v<LeadingTag>);
    EXPECT_EQ(diag::categories_v[diag::catalog_size - 1], diag::category_of_v<TrailingTag>);

    // The array index and the enumerator value have to stay the same
    // number, because the accessors above index by enumerator.
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(diag::categories_v[i], static_cast<diag::Category>(i));
    }
}

void test_enumerate_categories_visits_all() {
    constexpr std::size_t expected = diag::catalog_size;
    constexpr std::size_t observed = []() consteval {
        std::size_t n = 0;
        diag::enumerate_categories([&n]<diag::Category /*C*/>() noexcept { ++n; });
        return n;
    }();
    static_assert(observed == expected, "enumerate_categories did not visit every Category");

    // The count alone would pass for a visitor that visited one entry
    // many times, so the same walk is repeated for its order.
    std::array<std::string_view, diag::catalog_size> visited{};
    std::size_t cursor = 0;
    diag::enumerate_categories(
        [&visited, &cursor]<diag::Category C>() noexcept { visited[cursor++] = diag::name_of(C); });
    EXPECT_EQ(cursor, diag::catalog_size);
    EXPECT_EQ(visited.front(), LeadingTag::name);
    EXPECT_EQ(visited.back(), TrailingTag::name);
}

void test_mint_diagnostic_factory() {
    auto d1 = diag::mint_diagnostic<diag::EffectRowMismatch>(int{}, float{});
    using d1_t = decltype(d1);
    static_assert(std::is_same_v<d1_t, diag::Diagnostic<diag::EffectRowMismatch, int, float>>);
    EXPECT_EQ(d1_t::name, std::string_view{"EffectRowMismatch"});

    auto d2 = diag::mint_diagnostic<diag::HotPathViolation>();
    using d2_t = decltype(d2);
    static_assert(std::is_same_v<d2_t, diag::Diagnostic<diag::HotPathViolation>>);
    EXPECT_EQ(d2_t::name, std::string_view{"HotPathViolation"});

    // The argument is const here and the deduced context type is not:
    // the factory strips qualifiers, so two call sites that differ only
    // in constness produce one type.
    int const x = 7;
    auto d3 = diag::mint_diagnostic<diag::DetSafeLeak>(x);
    static_assert(std::is_same_v<decltype(d3), diag::Diagnostic<diag::DetSafeLeak, int>>);
}

// The accessors are switches written by hand alongside the tags.  These
// comparisons are what catch an arm that answers for the wrong tag.
void test_diagnostic_accessor_strings_match_tag_fields() {
    EXPECT_EQ(diag::name_of(diag::Category::EffectRowMismatch), diag::EffectRowMismatch::name);
    EXPECT_EQ(diag::name_of(diag::Category::DetSafeLeak), diag::DetSafeLeak::name);
    EXPECT_EQ(diag::name_of(diag::Category::RecipeSpecMismatch), diag::RecipeSpecMismatch::name);
    EXPECT_EQ(diag::description_of(diag::Category::DetSafeLeak), diag::DetSafeLeak::description);
    EXPECT_EQ(diag::remediation_of(diag::Category::DetSafeLeak), diag::DetSafeLeak::remediation);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_diagnostic_compile:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_catalog_cardinality", test_catalog_cardinality);
    run_test("test_tag_inheritance_detection", test_tag_inheritance_detection);
    run_test("test_accessor_runtime_coverage", test_accessor_runtime_coverage);
    run_test("test_bidirectional_map", test_bidirectional_map);
    run_test("test_diagnostic_wrapper", test_diagnostic_wrapper);
    run_test("test_macro_compiles", test_macro_compiles);
    run_test("test_categories_array", test_categories_array);
    run_test("test_enumerate_categories_visits_all", test_enumerate_categories_visits_all);
    run_test("test_mint_diagnostic_factory", test_mint_diagnostic_factory);
    run_test("test_diagnostic_accessor_strings_match_tag_fields", test_diagnostic_accessor_strings_match_tag_fields);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
