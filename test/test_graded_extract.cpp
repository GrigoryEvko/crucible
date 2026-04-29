// ═══════════════════════════════════════════════════════════════════
// test_graded_extract — sentinel TU for safety/GradedExtract.h
//
// Same blind-spot rationale as test_is_owned_region / test_is_
// permission / test_signature_traits (see feedback_header_only_
// static_assert_blind_spot memory): a header shipped with embedded
// static_asserts is unverified under the project warning flags
// unless a .cpp TU includes it.  This sentinel forces
// GradedExtract.h through the test target's full -Werror=shadow /
// -Werror=conversion / -Wanalyzer-* matrix and exercises the
// runtime_smoke_test inline body.
//
// Coverage (BROADER than the header self-test, which uses only a
// minimal witness to avoid TU-context-fragility):
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution.
//   * All four ModalityKind values:
//       - Absolute     — Linear / Refined / Stale / Monotonic /
//                        AppendOnly / DetSafe / HotPath / etc.
//       - Comonad      — Secret
//       - RelativeMonad — Tagged
//       - Relative     — RESERVED slot, no production wrapper yet
//   * Three storage regimes:
//       - regime-1 (EBO collapse) — Linear, Refined, DetSafe
//       - regime-3 (derived grade, value_type_decoupled) — AppendOnly
//       - regime-4 (T + grade) — Stale, TimeOrdered
//   * Cv-ref stripping — &, &&, const, volatile, all combinations.
//   * Negative — bare types, non-wrapper structs, primitives.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/GradedExtract.h>

#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/permissions/Permission.h>

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
using ::crucible::algebra::ModalityKind;

struct PositiveCheck {
    constexpr bool operator()(int v) const noexcept { return v > 0; }
};
inline constexpr PositiveCheck positive_local{};

struct test_source_x {};
struct test_provenance_y {};

void test_runtime_smoke() {
    EXPECT_TRUE(extract::runtime_smoke_test());
}

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

    // Cv-ref stripping under the concept gate.
    static_assert(extract::IsGradedWrapper<Linear<int>&>);
    static_assert(extract::IsGradedWrapper<Linear<int>&&>);
    static_assert(extract::IsGradedWrapper<Linear<int> const&>);

    // Bare types are not GradedWrappers.
    static_assert(!extract::IsGradedWrapper<int>);
    static_assert(!extract::IsGradedWrapper<int*>);
    static_assert(!extract::IsGradedWrapper<void>);
    struct Lookalike { using value_type = int; };  // missing surface
    static_assert(!extract::IsGradedWrapper<Lookalike>);
}

void test_value_type_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;
    using ::crucible::safety::Stale;

    static_assert(std::is_same_v<extract::value_type_of_t<Linear<int>>,    int>);
    static_assert(std::is_same_v<extract::value_type_of_t<Linear<double>>, double>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Refined<positive_local, int>>, int>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Tagged<float, test_source_x>>, float>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Secret<long long>>, long long>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Stale<double>>, double>);

    // Cv-ref stripping.
    static_assert(std::is_same_v<
        extract::value_type_of_t<Linear<int>&>, int>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Linear<int> const&>, int>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Linear<int>&&>, int>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<Linear<int> const&&>, int>);
}

void test_value_type_cheat1_appendonly() {
    using ::crucible::safety::AppendOnly;
    // CHEAT-1 case: AppendOnly's user-facing value_type is T (the
    // element), NOT graded_type::value_type which is Storage<T>
    // (the container).  The dispatcher sees what the USER declared.
    static_assert(std::is_same_v<
        extract::value_type_of_t<AppendOnly<int>>, int>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<AppendOnly<double>>, double>);
    // Substrate's view differs:
    static_assert(!std::is_same_v<
        extract::value_type_of_t<AppendOnly<int>>,
        typename AppendOnly<int>::graded_type::value_type>);
}

void test_lattice_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Stale;

    // Each wrapper's lattice_of_t projects exactly its
    // declared lattice_type.
    static_assert(std::is_same_v<
        extract::lattice_of_t<Linear<int>>,
        typename Linear<int>::lattice_type>);
    static_assert(std::is_same_v<
        extract::lattice_of_t<Refined<positive_local, int>>,
        typename Refined<positive_local, int>::lattice_type>);
    static_assert(std::is_same_v<
        extract::lattice_of_t<Stale<double>>,
        typename Stale<double>::lattice_type>);

    // Cv-ref stripping.
    static_assert(std::is_same_v<
        extract::lattice_of_t<Linear<int>&>,
        typename Linear<int>::lattice_type>);
    static_assert(std::is_same_v<
        extract::lattice_of_t<Linear<int> const&&>,
        typename Linear<int>::lattice_type>);
}

void test_grade_extraction() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Stale;

    // grade_of_t<W> == LatticeElement<lattice_of_t<W>> ==
    // W::graded_type::grade_type.
    static_assert(std::is_same_v<
        extract::grade_of_t<Linear<int>>,
        typename Linear<int>::graded_type::grade_type>);
    static_assert(std::is_same_v<
        extract::grade_of_t<Stale<double>>,
        typename Stale<double>::graded_type::grade_type>);

    // Cv-ref stripping.
    static_assert(std::is_same_v<
        extract::grade_of_t<Stale<double>&&>,
        typename Stale<double>::graded_type::grade_type>);
}

void test_modality_extraction_all_four() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;
    using ::crucible::safety::Stale;
    using ::crucible::safety::Monotonic;
    using ::crucible::safety::AppendOnly;

    // Absolute modality — the bulk of wrappers.
    static_assert(extract::modality_of_v<Linear<int>>
                  == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Refined<positive_local, int>>
                  == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Stale<int>>
                  == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<Monotonic<std::uint64_t>>
                  == ModalityKind::Absolute);
    static_assert(extract::modality_of_v<AppendOnly<int>>
                  == ModalityKind::Absolute);

    // RelativeMonad — Tagged carries provenance via unit-in.
    static_assert(extract::modality_of_v<Tagged<int, test_source_x>>
                  == ModalityKind::RelativeMonad);

    // Comonad — Secret carries classification via counit-out.
    static_assert(extract::modality_of_v<Secret<int>>
                  == ModalityKind::Comonad);

    // Relative — RESERVED slot per Modality.h:51; no production
    // wrapper occupies it yet.  When FOUND-H03 ships
    // Computation<R, T> as a Graded alias under Modality::Relative,
    // we add a positive case here.

    // Cv-ref stripping preserves modality.
    static_assert(extract::modality_of_v<Secret<int>&&>
                  == ModalityKind::Comonad);
    static_assert(extract::modality_of_v<Tagged<int, test_source_x> const&>
                  == ModalityKind::RelativeMonad);
}

void test_grade_distinguishes_singletons() {
    using ::crucible::safety::DetSafe;
    using ::crucible::safety::HotPath;
    using ::crucible::algebra::lattices::DetSafeTier;
    using ::crucible::algebra::lattices::HotPathTier;

    // DetSafe<Pure, T> and DetSafe<PhiloxRng, T> — same lattice
    // FAMILY but distinct singleton sub-lattices.  Their lattice_of_t
    // projects DIFFERENT types (each At<Tier> is a different nested
    // type).  This is the type-level discrimination the dispatcher
    // uses to refuse Pure-pinned consumers from accepting PhiloxRng-
    // graded values.
    static_assert(!std::is_same_v<
        extract::lattice_of_t<DetSafe<DetSafeTier::Pure, int>>,
        extract::lattice_of_t<DetSafe<DetSafeTier::PhiloxRng, int>>>);
    static_assert(!std::is_same_v<
        extract::lattice_of_t<HotPath<HotPathTier::Hot,  int>>,
        extract::lattice_of_t<HotPath<HotPathTier::Cold, int>>>);

    // Same wrapper + same singleton = same lattice.
    static_assert(std::is_same_v<
        extract::lattice_of_t<DetSafe<DetSafeTier::Pure, int>>,
        extract::lattice_of_t<DetSafe<DetSafeTier::Pure, double>>>);
}

void test_distinct_wrappers_distinct_modalities() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Tagged;
    using ::crucible::safety::Secret;

    // The three modalities Crucible currently uses are mutually
    // distinguishable at the trait level.
    static_assert(extract::modality_of_v<Linear<int>>
                  != extract::modality_of_v<Tagged<int, test_source_x>>);
    static_assert(extract::modality_of_v<Tagged<int, test_source_x>>
                  != extract::modality_of_v<Secret<int>>);
    static_assert(extract::modality_of_v<Linear<int>>
                  != extract::modality_of_v<Secret<int>>);
}

void test_shared_permission_facade() {
    // Regime-5: SharedPermission<Tag> is a 1-byte phantom token; the
    // atomic refcount lives in SharedPermissionPool<Tag>.  The
    // wrapper exposes graded_type for diagnostic introspection
    // (Permission.h:493-537 façade rationale) and conforms to
    // GradedWrapper despite being structurally distinct from the
    // four other regimes.
    struct sp_test_tag {};
    using SP = ::crucible::safety::SharedPermission<sp_test_tag>;

    static_assert(extract::IsGradedWrapper<SP>);
    static_assert(extract::IsGradedWrapper<SP&>);
    static_assert(extract::IsGradedWrapper<SP const&>);
    static_assert(extract::is_graded_wrapper_v<SP>);

    // value_type for the façade is Tag itself (the region label).
    static_assert(std::is_same_v<
        extract::value_type_of_t<SP>, sp_test_tag>);

    static_assert(extract::modality_of_v<SP>
                  == ModalityKind::Absolute);
}

void test_product_lattice_wrapper_budgeted() {
    // Budgeted<{BitsBudget, PeakBytes}, T> is a regime-4-shape
    // wrapper over ProductLattice<BitsBudgetLattice, PeakBytesLattice>.
    // Validates that the universal extractors handle product lattices
    // — the lattice_type is itself a composite, and grade_type is a
    // tuple-like element.  This is the path that FOUND-D10
    // (inferred_row_t) will traverse when composing per-axis grades.
    using ::crucible::safety::Budgeted;

    static_assert(extract::IsGradedWrapper<Budgeted<int>>);
    static_assert(extract::IsGradedWrapper<Budgeted<int>&&>);
    static_assert(extract::is_graded_wrapper_v<Budgeted<int>>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<Budgeted<int>>, int>);

    static_assert(extract::modality_of_v<Budgeted<int>>
                  == ModalityKind::Absolute);

    // graded_type_of_t projects the substrate's bare Graded<...>.
    // For Budgeted the substrate's lattice is the ProductLattice
    // composition; the universal extractor MUST surface it without
    // collapsing.
    static_assert(extract::is_graded_specialization_v<
        extract::graded_type_of_t<Budgeted<int>>>);
}

void test_nested_wrappers_satisfy_concept() {
    // Linear<Refined<P, T>> is itself a GradedWrapper (Linear is one,
    // independent of what it wraps).  The CONCEPT applies to the
    // OUTERMOST wrapper only — Linear's value_type is Refined<P, T>.
    // The dispatcher harvests Linear's lattice/grade/modality and
    // ignores what the inside layer carries; if the dispatcher wants
    // the inner layer it recurses into value_type_of_t.
    using ::crucible::safety::Linear;
    using ::crucible::safety::Refined;
    using ::crucible::safety::Tagged;

    using L_R = Linear<Refined<positive_local, int>>;
    static_assert(extract::IsGradedWrapper<L_R>);
    static_assert(extract::is_graded_wrapper_v<L_R>);

    // Outermost surfaces — Linear's slots project, NOT Refined's.
    static_assert(std::is_same_v<
        extract::value_type_of_t<L_R>,
        Refined<positive_local, int>>);
    static_assert(extract::modality_of_v<L_R>
                  == ModalityKind::Absolute);
    static_assert(std::is_same_v<
        extract::lattice_of_t<L_R>,
        typename Linear<int>::lattice_type>);

    // Recursing into the value_type re-enters the dispatcher's
    // reading surface — the inner Refined IS itself a GradedWrapper.
    static_assert(extract::IsGradedWrapper<
        extract::value_type_of_t<L_R>>);
    static_assert(extract::modality_of_v<
        extract::value_type_of_t<L_R>>
        == ModalityKind::Absolute);

    // Triply-nested Tagged<Linear<Refined<P, T>>, S> follows the
    // same rule — RelativeMonad modality at the OUTERMOST layer.
    using T_L_R = Tagged<L_R, test_source_x>;
    static_assert(extract::IsGradedWrapper<T_L_R>);
    static_assert(extract::modality_of_v<T_L_R>
                  == ModalityKind::RelativeMonad);
}

void test_graded_specialization_predicate() {
    using ::crucible::safety::Linear;

    // Bare types and lookalike structs are not specializations.
    static_assert(!extract::is_graded_specialization_v<int>);
    static_assert(!extract::is_graded_specialization_v<int*>);
    static_assert(!extract::is_graded_specialization_v<void>);

    // The bare substrate carrier IS a specialization.
    static_assert(extract::is_graded_specialization_v<
        typename Linear<int>::graded_type>);
    static_assert(extract::is_graded_specialization_v<
        typename Linear<int>::graded_type const&>);

    // The WRAPPER is NOT itself a specialization (it wraps one).
    // This distinction is load-bearing for the dispatcher: a bare
    // Graded<...> passed where a wrapper is expected is a category
    // error (the substrate has no wrapper-level forwarders).
    static_assert(!extract::is_graded_specialization_v<Linear<int>>);
}

void test_runtime_consistency() {
    using ::crucible::safety::Linear;
    using ::crucible::safety::Stale;

    volatile std::size_t const cap = 50;
    bool baseline_int = std::is_same_v<
        extract::value_type_of_t<Linear<int>>, int>;
    bool baseline_dbl = std::is_same_v<
        extract::value_type_of_t<Stale<double>>, double>;
    EXPECT_TRUE(baseline_int);
    EXPECT_TRUE(baseline_dbl);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_int == (std::is_same_v<
            extract::value_type_of_t<Linear<int>>, int>));
        EXPECT_TRUE(baseline_dbl == (std::is_same_v<
            extract::value_type_of_t<Stale<double>>, double>));
        EXPECT_TRUE(extract::IsGradedWrapper<Linear<int>>);
        EXPECT_TRUE(!extract::IsGradedWrapper<int>);
        EXPECT_TRUE(extract::modality_of_v<Linear<int>>
                    == ModalityKind::Absolute);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_graded_extract:\n");
    run_test("test_runtime_smoke",                  test_runtime_smoke);
    run_test("test_concept_form_real_wrappers",     test_concept_form_real_wrappers);
    run_test("test_value_type_extraction",          test_value_type_extraction);
    run_test("test_value_type_cheat1_appendonly",   test_value_type_cheat1_appendonly);
    run_test("test_lattice_extraction",             test_lattice_extraction);
    run_test("test_grade_extraction",               test_grade_extraction);
    run_test("test_modality_extraction_all_four",   test_modality_extraction_all_four);
    run_test("test_grade_distinguishes_singletons", test_grade_distinguishes_singletons);
    run_test("test_distinct_wrappers_distinct_modalities",
                                                    test_distinct_wrappers_distinct_modalities);
    run_test("test_shared_permission_facade",       test_shared_permission_facade);
    run_test("test_product_lattice_wrapper_budgeted",
                                                    test_product_lattice_wrapper_budgeted);
    run_test("test_nested_wrappers_satisfy_concept",
                                                    test_nested_wrappers_satisfy_concept);
    run_test("test_graded_specialization_predicate",
                                                    test_graded_specialization_predicate);
    run_test("test_runtime_consistency",            test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
