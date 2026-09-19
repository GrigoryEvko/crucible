#include <crucible/safety/GradedExtract.h>

#include <crucible/safety/_Linear.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/permissions/_Permission.h>

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
using ::crucible::algebra::ModalityKind;

struct PositiveCheck {
    constexpr bool operator()(int v) const noexcept { return v > 0; }
};
inline constexpr PositiveCheck positive_local{};

struct test_source_x {};
struct test_provenance_y {};

void test_runtime_smoke() { EXPECT_TRUE(extract::graded_extract_smoke_test()); }

void test_concept_form_real_wrappers() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;
    using ::crucible::safety::Stale;
    using ::crucible::safety::Monotonic;
    using ::crucible::safety::AppendOnly;
    using ::crucible::safety::HotPath;
    using ::crucible::safety::DetSafe;
    using ::crucible::safety::AllocClass;
    using ::crucible::algebra::lattices::HotPathTier;
    using ::crucible::algebra::lattices::DetSafeTier;
    using ::crucible::algebra::lattices::AllocClassTag;

    static_assert(extract::IsGradedWrapper<Linear<int>>);
    static_assert(extract::IsGradedWrapper<Refined<positive_local, int>>);
    static_assert(extract::IsGradedWrapper<Tagged<int, test_source_x>>);
    static_assert(extract::IsGradedWrapper<Secret<int>>);
    static_assert(extract::IsGradedWrapper<Stale<double>>);
    static_assert(extract::IsGradedWrapper<Monotonic<std::uint64_t>>);
    static_assert(extract::IsGradedWrapper<AppendOnly<int>>);
    static_assert(extract::IsGradedWrapper<HotPath<HotPathTier::Hot, int>>);
    static_assert(extract::IsGradedWrapper<DetSafe<DetSafeTier::Pure, int>>);
    static_assert(extract::IsGradedWrapper<AllocClass<AllocClassTag::Stack, int>>);

    static_assert(extract::IsGradedWrapper<Linear<int>&>);
    static_assert(extract::IsGradedWrapper<Linear<int>&&>);
    static_assert(extract::IsGradedWrapper<Linear<int> const&>);

    static_assert(!extract::IsGradedWrapper<int>);
    static_assert(!extract::IsGradedWrapper<int*>);
    static_assert(!extract::IsGradedWrapper<void>);
    struct Lookalike {
        using value_type = int;
    };  // missing surface
    static_assert(!extract::IsGradedWrapper<Lookalike>);
}

void test_value_type_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;
    using ::crucible::safety::Stale;

    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int>>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Linear<double>>, double>);
    static_assert(std::is_same_v<extract::value_type_of_t<Refined<positive_local, int>>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Tagged<float, test_source_x>>, float>);
    static_assert(std::is_same_v<extract::value_type_of_t<Secret<long long>>, long long>);
    static_assert(std::is_same_v<extract::value_type_of_t<Stale<double>>, double>);

    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int>&>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int> const&>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int>&&>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int> const&&>, int>);
}

void test_value_type_cheat1_appendonly() {
    using ::crucible::safety::AppendOnly;
    // The user-facing value_type is the element, while the substrate's
    // is the container.  The dispatcher sees what the user declared.
    static_assert(std::is_same_v<extract::value_type_of_t<AppendOnly<int>>, int>);
    static_assert(std::is_same_v<extract::value_type_of_t<AppendOnly<double>>, double>);
    static_assert(
        !std::is_same_v<extract::value_type_of_t<AppendOnly<int>>, typename AppendOnly<int>::graded_type::value_type>);
}

void test_lattice_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Stale;

    static_assert(std::is_same_v<extract::lattice_of_t<Linear<int>>, typename Linear<int>::lattice_type>);
    static_assert(std::is_same_v<extract::lattice_of_t<Refined<positive_local, int>>,
                                 typename Refined<positive_local, int>::lattice_type>);
    static_assert(std::is_same_v<extract::lattice_of_t<Stale<double>>, typename Stale<double>::lattice_type>);

    static_assert(std::is_same_v<extract::lattice_of_t<Linear<int>&>, typename Linear<int>::lattice_type>);
    static_assert(std::is_same_v<extract::lattice_of_t<Linear<int> const&&>, typename Linear<int>::lattice_type>);
}

void test_grade_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Stale;

    static_assert(std::is_same_v<extract::grade_of_t<Linear<int>>, typename Linear<int>::graded_type::grade_type>);
    static_assert(std::is_same_v<extract::grade_of_t<Stale<double>>, typename Stale<double>::graded_type::grade_type>);

    static_assert(
        std::is_same_v<extract::grade_of_t<Stale<double>&&>, typename Stale<double>::graded_type::grade_type>);
}

void test_modality_extraction_all_four() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;
    using ::crucible::safety::Stale;
    using ::crucible::safety::Monotonic;
    using ::crucible::safety::AppendOnly;

    static_assert(extract::modality_of_v<Linear<int>> == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Refined<positive_local, int>> == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Stale<int>> == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Monotonic<std::uint64_t>> == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<AppendOnly<int>> == ModalityKind::Absolute);

    static_assert(extract::modality_of_v<Tagged<int, test_source_x>> == ModalityKind::RelativeMonad);

    static_assert(extract::modality_of_v<Secret<int>> == ModalityKind::Comonad);

    // The fourth modality, Relative, has no production wrapper, so
    // there is no positive case for it here.

    static_assert(extract::modality_of_v<Secret<int>&&> == ModalityKind::Comonad);
    static_assert(extract::modality_of_v<Tagged<int, test_source_x> const&> == ModalityKind::RelativeMonad);
}

void test_grade_distinguishes_singletons() {
    using ::crucible::safety::DetSafe;
    using ::crucible::safety::HotPath;
    using ::crucible::algebra::lattices::DetSafeTier;
    using ::crucible::algebra::lattices::HotPathTier;

    // The same lattice family, but each tier is a distinct nested type.
    // That is what lets a consumer pinned to one tier refuse a value
    // graded at another.
    static_assert(!std::is_same_v<extract::lattice_of_t<DetSafe<DetSafeTier::Pure, int>>,
                                  extract::lattice_of_t<DetSafe<DetSafeTier::PhiloxRng, int>>>);
    static_assert(!std::is_same_v<extract::lattice_of_t<HotPath<HotPathTier::Hot, int>>,
                                  extract::lattice_of_t<HotPath<HotPathTier::Cold, int>>>);

    static_assert(std::is_same_v<extract::lattice_of_t<DetSafe<DetSafeTier::Pure, int>>,
                                 extract::lattice_of_t<DetSafe<DetSafeTier::Pure, double>>>);
}

void test_distinct_wrappers_distinct_modalities() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;

    static_assert(extract::modality_of_v<Linear<int>> != extract::modality_of_v<Tagged<int, test_source_x>>);
    static_assert(extract::modality_of_v<Tagged<int, test_source_x>> != extract::modality_of_v<Secret<int>>);
    static_assert(extract::modality_of_v<Linear<int>> != extract::modality_of_v<Secret<int>>);
}

void test_shared_permission_facade() {
    // The token is a one-byte phantom and the refcount lives in its
    // pool, so this wrapper conforms to the concept while being
    // structurally unlike the others.
    struct sp_test_tag {};
    using SP = ::crucible::safety::SharedPermission<sp_test_tag>;

    static_assert(extract::IsGradedWrapper<SP>);
    static_assert(extract::IsGradedWrapper<SP&>);
    static_assert(extract::IsGradedWrapper<SP const&>);
    static_assert(extract::is_graded_wrapper_v<SP>);

    // The value type is the tag itself, the region label.
    static_assert(std::is_same_v<extract::value_type_of_t<SP>, sp_test_tag>);

    static_assert(extract::modality_of_v<SP> == ModalityKind::Absolute);
}

void test_product_lattice_wrapper_budgeted() {
    // A product lattice: the lattice type is itself a composite and the
    // grade is tuple-like.  The extractors must handle that shape.
    using ::crucible::safety::Budgeted;

    static_assert(extract::IsGradedWrapper<Budgeted<int>>);
    static_assert(extract::IsGradedWrapper<Budgeted<int>&&>);
    static_assert(extract::is_graded_wrapper_v<Budgeted<int>>);

    static_assert(std::is_same_v<extract::value_type_of_t<Budgeted<int>>, int>);

    static_assert(extract::modality_of_v<Budgeted<int>> == ModalityKind::Absolute);

    // The extractor must surface the composite lattice without
    // collapsing it.
    static_assert(extract::is_graded_specialization_v<extract::graded_type_of_t<Budgeted<int>>>);
}

void test_nested_wrappers_satisfy_concept() {
    // The traits read the outermost wrapper only.  Reaching the inner
    // layer means recursing through value_type_of_t.
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;

    using L_R = Linear<Refined<positive_local, int>>;
    static_assert(extract::IsGradedWrapper<L_R>);
    static_assert(extract::is_graded_wrapper_v<L_R>);

    static_assert(std::is_same_v<extract::value_type_of_t<L_R>, Refined<positive_local, int>>);
    static_assert(extract::modality_of_v<L_R> == ModalityKind::Absolute);
    static_assert(std::is_same_v<extract::lattice_of_t<L_R>, typename Linear<int>::lattice_type>);

    static_assert(extract::IsGradedWrapper<extract::value_type_of_t<L_R>>);
    static_assert(extract::modality_of_v<extract::value_type_of_t<L_R>> == ModalityKind::Absolute);

    using T_L_R = Tagged<L_R, test_source_x>;
    static_assert(extract::IsGradedWrapper<T_L_R>);
    static_assert(extract::modality_of_v<T_L_R> == ModalityKind::RelativeMonad);
}

void test_graded_specialization_predicate() {
    using ::crucible::safety::Linear;

    static_assert(!extract::is_graded_specialization_v<int>);
    static_assert(!extract::is_graded_specialization_v<int*>);
    static_assert(!extract::is_graded_specialization_v<void>);

    static_assert(extract::is_graded_specialization_v<typename Linear<int>::graded_type>);
    static_assert(extract::is_graded_specialization_v<typename Linear<int>::graded_type const&>);

    // A wrapper is not itself a specialization: it holds one.  Passing a
    // bare substrate carrier where a wrapper is expected is a category
    // error, since the substrate has no wrapper-level forwarders.
    static_assert(!extract::is_graded_specialization_v<Linear<int>>);
}

void test_runtime_consistency() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Stale;

    volatile std::size_t const cap = 50;
    bool baseline_int = std::is_same_v<extract::value_type_of_t<Linear<int>>, int>;
    bool baseline_dbl = std::is_same_v<extract::value_type_of_t<Stale<double>>, double>;
    EXPECT_TRUE(baseline_int);
    EXPECT_TRUE(baseline_dbl);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_int == (std::is_same_v<extract::value_type_of_t<Linear<int>>, int>));
        EXPECT_TRUE(baseline_dbl == (std::is_same_v<extract::value_type_of_t<Stale<double>>, double>));
        EXPECT_TRUE(extract::IsGradedWrapper<Linear<int>>);
        EXPECT_TRUE(!extract::IsGradedWrapper<int>);
        EXPECT_TRUE(extract::modality_of_v<Linear<int>> == ModalityKind::Absolute);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_graded_extract:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_concept_form_real_wrappers", test_concept_form_real_wrappers);
    run_test("test_value_type_extraction", test_value_type_extraction);
    run_test("test_value_type_cheat1_appendonly", test_value_type_cheat1_appendonly);
    run_test("test_lattice_extraction", test_lattice_extraction);
    run_test("test_grade_extraction", test_grade_extraction);
    run_test("test_modality_extraction_all_four", test_modality_extraction_all_four);
    run_test("test_grade_distinguishes_singletons", test_grade_distinguishes_singletons);
    run_test("test_distinct_wrappers_distinct_modalities", test_distinct_wrappers_distinct_modalities);
    run_test("test_shared_permission_facade", test_shared_permission_facade);
    run_test("test_product_lattice_wrapper_budgeted", test_product_lattice_wrapper_budgeted);
    run_test("test_nested_wrappers_satisfy_concept", test_nested_wrappers_satisfy_concept);
    run_test("test_graded_specialization_predicate", test_graded_specialization_predicate);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
