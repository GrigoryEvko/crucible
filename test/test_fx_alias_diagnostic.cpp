#include <crucible/safety/_Diagnostic.h>

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace diag = ::crucible::safety::diag;

void test_catalog_indices_for_fx_alias_tags() {
    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::PureFunctionViolation>, diag::PureFunctionViolation>);
    static_assert(diag::category_of_v<diag::PureFunctionViolation> == diag::Category::PureFunctionViolation);

    static_assert(
        std::is_same_v<diag::tag_of_t<diag::Category::DivergenceBudgetViolation>, diag::DivergenceBudgetViolation>);
    static_assert(diag::category_of_v<diag::DivergenceBudgetViolation> == diag::Category::DivergenceBudgetViolation);

    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::StateBudgetViolation>, diag::StateBudgetViolation>);
    static_assert(diag::category_of_v<diag::StateBudgetViolation> == diag::Category::StateBudgetViolation);

    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::InsufficientWitness>, diag::InsufficientWitness>);
    static_assert(diag::category_of_v<diag::InsufficientWitness> == diag::Category::InsufficientWitness);

    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::ModalityMismatch>, diag::ModalityMismatch>);
    static_assert(diag::category_of_v<diag::ModalityMismatch> == diag::Category::ModalityMismatch);
    static_assert(std::is_same_v<diag::tag_of_t<diag::Category::LinearAliasViolation>, diag::LinearAliasViolation>);
    static_assert(diag::category_of_v<diag::LinearAliasViolation> == diag::Category::LinearAliasViolation);

    // The catalog is append-only, so its size only grows.  A floor
    // rather than an equality keeps appending a tag from redding this
    // test.
    EXPECT_TRUE(diag::catalog_size >= 30);

    // Membership by bound rather than by index, for the same reason.
    EXPECT_TRUE(static_cast<std::size_t>(diag::Category::HugePageAllocationFailed) < diag::catalog_size);
}

void test_runtime_accessors_route_to_fx_alias_strings() {
    EXPECT_TRUE(diag::name_of(diag::Category::PureFunctionViolation) == diag::PureFunctionViolation::name);
    EXPECT_TRUE(diag::name_of(diag::Category::DivergenceBudgetViolation) == diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::name_of(diag::Category::StateBudgetViolation) == diag::StateBudgetViolation::name);

    EXPECT_TRUE(diag::description_of(diag::Category::PureFunctionViolation)
                == diag::PureFunctionViolation::description);
    EXPECT_TRUE(diag::description_of(diag::Category::DivergenceBudgetViolation)
                == diag::DivergenceBudgetViolation::description);
    EXPECT_TRUE(diag::description_of(diag::Category::StateBudgetViolation) == diag::StateBudgetViolation::description);

    EXPECT_TRUE(diag::remediation_of(diag::Category::PureFunctionViolation)
                == diag::PureFunctionViolation::remediation);
    EXPECT_TRUE(diag::remediation_of(diag::Category::DivergenceBudgetViolation)
                == diag::DivergenceBudgetViolation::remediation);
    EXPECT_TRUE(diag::remediation_of(diag::Category::StateBudgetViolation) == diag::StateBudgetViolation::remediation);
}

void test_fx_alias_tag_strings_are_substantive() {
    // An empty or stub string passes every compile-time check and
    // breaks the diagnostic surface only at runtime.  The size floors
    // demand a real sentence rather than a placeholder.
    EXPECT_TRUE(diag::PureFunctionViolation::name.size() > 0);
    EXPECT_TRUE(diag::PureFunctionViolation::description.size() > 50);
    EXPECT_TRUE(diag::PureFunctionViolation::remediation.size() > 50);

    EXPECT_TRUE(diag::DivergenceBudgetViolation::name.size() > 0);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::description.size() > 50);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::remediation.size() > 50);

    EXPECT_TRUE(diag::StateBudgetViolation::name.size() > 0);
    EXPECT_TRUE(diag::StateBudgetViolation::description.size() > 50);
    EXPECT_TRUE(diag::StateBudgetViolation::remediation.size() > 50);

    // Pairwise distinct names catch a tag copy-pasted without renaming.
    EXPECT_TRUE(diag::PureFunctionViolation::name != diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::PureFunctionViolation::name != diag::StateBudgetViolation::name);
    EXPECT_TRUE(diag::DivergenceBudgetViolation::name != diag::StateBudgetViolation::name);
}

void test_fx_alias_tags_are_diagnostic_classes() {
    static_assert(diag::is_diagnostic_class_v<diag::PureFunctionViolation>);
    static_assert(diag::is_diagnostic_class_v<diag::DivergenceBudgetViolation>);
    static_assert(diag::is_diagnostic_class_v<diag::StateBudgetViolation>);

    EXPECT_TRUE(diag::diagnostic_name_v<diag::PureFunctionViolation> == diag::PureFunctionViolation::name);
    EXPECT_TRUE(diag::diagnostic_name_v<diag::DivergenceBudgetViolation> == diag::DivergenceBudgetViolation::name);
    EXPECT_TRUE(diag::diagnostic_name_v<diag::StateBudgetViolation> == diag::StateBudgetViolation::name);
}

void test_diagnostic_wrapper_for_fx_alias_tags() {
    using D1 = diag::Diagnostic<diag::PureFunctionViolation, int>;
    using D2 = diag::Diagnostic<diag::DivergenceBudgetViolation, double>;
    using D3 = diag::Diagnostic<diag::StateBudgetViolation, char, long>;
    static_assert(std::is_default_constructible_v<D1>);
    static_assert(std::is_default_constructible_v<D2>);
    static_assert(std::is_default_constructible_v<D3>);
    // The static_asserts above are the claim.  This gives run_test a body.
    EXPECT_TRUE(true);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fx_alias_diagnostic:\n");
    run_test("test_catalog_indices_for_fx_alias_tags", test_catalog_indices_for_fx_alias_tags);
    run_test("test_runtime_accessors_route_to_fx_alias_strings", test_runtime_accessors_route_to_fx_alias_strings);
    run_test("test_fx_alias_tag_strings_are_substantive", test_fx_alias_tag_strings_are_substantive);
    run_test("test_fx_alias_tags_are_diagnostic_classes", test_fx_alias_tags_are_diagnostic_classes);
    run_test("test_diagnostic_wrapper_for_fx_alias_tags", test_diagnostic_wrapper_for_fx_alias_tags);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
