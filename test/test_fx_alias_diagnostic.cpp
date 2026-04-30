// ═══════════════════════════════════════════════════════════════════
// test_fx_alias_diagnostic — FOUND-E18 dedicated test
//
// Exercises the three F*-style alias diagnostic categories
// (PureFunctionViolation, DivergenceBudgetViolation,
// StateBudgetViolation) registered in Diagnostic.h.  Pins the
// catalog ↔ category bijection at the new entries and verifies the
// runtime accessors (name_of / description_of / remediation_of)
// route correctly for each.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Diagnostic.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
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

namespace diag = ::crucible::safety::diag;

// ── Bijection — Catalog ↔ Category at the three new indices ───────

void test_catalog_indices_for_fx_alias_tags() {
    // PureFunctionViolation lives at index 22 of the Catalog tuple.
    // (22-tag wrapper-axis prefix from FOUND-E01 + this is the 23rd.)
    static_assert(std::is_same_v<
        diag::tag_of_t<diag::Category::PureFunctionViolation>,
        diag::PureFunctionViolation>);
    static_assert(diag::category_of_v<diag::PureFunctionViolation>
                  == diag::Category::PureFunctionViolation);

    // DivergenceBudgetViolation at index 23.
    static_assert(std::is_same_v<
        diag::tag_of_t<diag::Category::DivergenceBudgetViolation>,
        diag::DivergenceBudgetViolation>);
    static_assert(diag::category_of_v<diag::DivergenceBudgetViolation>
                  == diag::Category::DivergenceBudgetViolation);

    // StateBudgetViolation at index 24 — the catalog's last entry.
    static_assert(std::is_same_v<
        diag::tag_of_t<diag::Category::StateBudgetViolation>,
        diag::StateBudgetViolation>);
    static_assert(diag::category_of_v<diag::StateBudgetViolation>
                  == diag::Category::StateBudgetViolation);

    EXPECT_TRUE(diag::catalog_size == 25);
    EXPECT_TRUE(static_cast<std::size_t>(diag::Category::StateBudgetViolation)
                == diag::catalog_size - 1);
}

// ── Accessor routing — runtime name/description/remediation ───────

void test_runtime_accessors_route_to_fx_alias_strings() {
    EXPECT_TRUE(diag::name_of(diag::Category::PureFunctionViolation)
                == diag::PureFunctionViolation::name);
    EXPECT_TRUE(diag::name_of(diag::Category::DivergenceBudgetViolation)
                == diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::name_of(diag::Category::StateBudgetViolation)
                == diag::StateBudgetViolation::name);

    EXPECT_TRUE(diag::description_of(diag::Category::PureFunctionViolation)
                == diag::PureFunctionViolation::description);
    EXPECT_TRUE(diag::description_of(diag::Category::DivergenceBudgetViolation)
                == diag::DivergenceBudgetViolation::description);
    EXPECT_TRUE(diag::description_of(diag::Category::StateBudgetViolation)
                == diag::StateBudgetViolation::description);

    EXPECT_TRUE(diag::remediation_of(diag::Category::PureFunctionViolation)
                == diag::PureFunctionViolation::remediation);
    EXPECT_TRUE(diag::remediation_of(diag::Category::DivergenceBudgetViolation)
                == diag::DivergenceBudgetViolation::remediation);
    EXPECT_TRUE(diag::remediation_of(diag::Category::StateBudgetViolation)
                == diag::StateBudgetViolation::remediation);
}

// ── Tag substance — non-empty strings, distinct names ─────────────

void test_fx_alias_tag_strings_are_substantive() {
    // Empty strings would slip past compile-time checks but break the
    // diagnostic surface at runtime.  Pin minimum-substance for the
    // three new tags directly through the accessor surface.
    EXPECT_TRUE(diag::PureFunctionViolation::name.size()        >  0);
    EXPECT_TRUE(diag::PureFunctionViolation::description.size() > 50);
    EXPECT_TRUE(diag::PureFunctionViolation::remediation.size() > 50);

    EXPECT_TRUE(diag::DivergenceBudgetViolation::name.size()        >  0);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::description.size() > 50);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::remediation.size() > 50);

    EXPECT_TRUE(diag::StateBudgetViolation::name.size()        >  0);
    EXPECT_TRUE(diag::StateBudgetViolation::description.size() > 50);
    EXPECT_TRUE(diag::StateBudgetViolation::remediation.size() > 50);

    // Three names must be pairwise distinct — same-tag-renamed bug.
    EXPECT_TRUE(diag::PureFunctionViolation::name
                != diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::PureFunctionViolation::name
                != diag::StateBudgetViolation::name);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::name
                != diag::StateBudgetViolation::name);
}

// ── Inheritance — every new tag participates in is_diagnostic_class_v

void test_fx_alias_tags_are_diagnostic_classes() {
    static_assert(diag::is_diagnostic_class_v<diag::PureFunctionViolation>);
    static_assert(diag::is_diagnostic_class_v<diag::DivergenceBudgetViolation>);
    static_assert(diag::is_diagnostic_class_v<diag::StateBudgetViolation>);

    // Generic accessor templates work for all three.
    EXPECT_TRUE(diag::diagnostic_name_v<diag::PureFunctionViolation>
                == diag::PureFunctionViolation::name);
    EXPECT_TRUE(diag::diagnostic_name_v<diag::DivergenceBudgetViolation>
                == diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::diagnostic_name_v<diag::StateBudgetViolation>
                == diag::StateBudgetViolation::name);
}

// ── Diagnostic<Tag, Ctx...> wrapper — instantiates for all three ──

void test_diagnostic_wrapper_for_fx_alias_tags() {
    using D1 = diag::Diagnostic<diag::PureFunctionViolation, int>;
    using D2 = diag::Diagnostic<diag::DivergenceBudgetViolation, double>;
    using D3 = diag::Diagnostic<diag::StateBudgetViolation, char, long>;
    static_assert(std::is_default_constructible_v<D1>);
    static_assert(std::is_default_constructible_v<D2>);
    static_assert(std::is_default_constructible_v<D3>);
    EXPECT_TRUE(true);  // confirms TU compiles + links
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fx_alias_diagnostic:\n");
    run_test("test_catalog_indices_for_fx_alias_tags",
             test_catalog_indices_for_fx_alias_tags);
    run_test("test_runtime_accessors_route_to_fx_alias_strings",
             test_runtime_accessors_route_to_fx_alias_strings);
    run_test("test_fx_alias_tag_strings_are_substantive",
             test_fx_alias_tag_strings_are_substantive);
    run_test("test_fx_alias_tags_are_diagnostic_classes",
             test_fx_alias_tags_are_diagnostic_classes);
    run_test("test_diagnostic_wrapper_for_fx_alias_tags",
             test_diagnostic_wrapper_for_fx_alias_tags);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
