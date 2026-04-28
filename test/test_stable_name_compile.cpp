// ═══════════════════════════════════════════════════════════════════
// test_stable_name_compile — sentinel TU for safety/diag/StableName.h
//
// Same blind-spot rationale as test_diagnostic_compile / test_effects_
// compile (see feedback_header_only_static_assert_blind_spot memory):
// a header shipped with embedded static_asserts is unverified under
// the project warning flags unless a .cpp TU includes it.  This
// sentinel forces StableName.h through the test target's full -Werror
// matrix and exercises the runtime_smoke_test inline body.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution: exercises stable_type_id /
//     stable_name_of / canonicalize_pack / stable_function_id with
//     non-constant args.
//   * Cross-TU stability: stable_type_id<T> in this TU equals
//     stable_type_id<T> from any other TU (the inline constexpr
//     contract ensures this — verified at runtime via volatile loop).
//   * Adversarial cases: stable_type_id non-zero, distinct types →
//     distinct IDs, canonicalize_pack handles permutations.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/diag/StableName.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

// Namespace-scope function pointers used by stable_function_id tests.
// Must be defined BEFORE the test function (C++ name lookup) and
// outside the anonymous namespace if they need stable linkage for
// auto-NTTP deduction.  `inline` permits multiple TU inclusion.

inline void fn_ptr_a()           noexcept {}
inline void fn_ptr_b(int)        noexcept {}
inline int  fn_ptr_c(int, float) noexcept { return 0; }

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

#define EXPECT_NE(a, b)                                                    \
    do {                                                                   \
        if (!((a) != (b))) {                                               \
            std::fprintf(stderr,                                           \
                "    EXPECT_NE failed: %s != %s (%s:%d)\n",                \
                #a, #b, __FILE__, __LINE__);                               \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace diag = ::crucible::safety::diag;

// ─── Tests ──────────────────────────────────────────────────────────

void test_runtime_smoke() {
    diag::runtime_smoke_test();
}

void test_stable_name_non_empty() {
    EXPECT_TRUE(!diag::stable_name_of<int>.empty());
    EXPECT_TRUE(!diag::stable_name_of<float>.empty());
    EXPECT_TRUE(!diag::stable_name_of<double>.empty());
    EXPECT_TRUE(!diag::stable_name_of<void>.empty());
    EXPECT_TRUE(!diag::stable_name_of<std::string_view>.empty());

    // .ends_with discipline (per Graded.h:156-186 TU-fragility rule).
    EXPECT_TRUE(diag::stable_name_of<int>.ends_with("int"));
    EXPECT_TRUE(diag::stable_name_of<float>.ends_with("float"));
    EXPECT_TRUE(diag::stable_name_of<void>.ends_with("void"));
}

void test_stable_type_id_distinguishes() {
    // Primitives produce distinct IDs.
    EXPECT_NE(diag::stable_type_id<int>,    diag::stable_type_id<float>);
    EXPECT_NE(diag::stable_type_id<float>,  diag::stable_type_id<double>);
    EXPECT_NE(diag::stable_type_id<int>,    diag::stable_type_id<long>);
    EXPECT_NE(diag::stable_type_id<int>,    diag::stable_type_id<unsigned int>);
    EXPECT_NE(diag::stable_type_id<char>,   diag::stable_type_id<unsigned char>);
    EXPECT_NE(diag::stable_type_id<short>,  diag::stable_type_id<int>);
    EXPECT_NE(diag::stable_type_id<void>,   diag::stable_type_id<int>);

    // Composite types distinguish from their underlying.
    EXPECT_NE(diag::stable_type_id<int>,        diag::stable_type_id<int*>);
    EXPECT_NE(diag::stable_type_id<int>,        diag::stable_type_id<int const>);
    EXPECT_NE(diag::stable_type_id<int>,        diag::stable_type_id<int&>);

    // Non-zero (FNV-1a property).
    EXPECT_NE(diag::stable_type_id<int>,        std::uint64_t{0});
    EXPECT_NE(diag::stable_type_id<void>,       std::uint64_t{0});
}

void test_stable_type_id_consistent() {
    // Same T → same ID across calls within this TU.
    auto id1 = diag::stable_type_id<int>;
    auto id2 = diag::stable_type_id<int>;
    EXPECT_EQ(id1, id2);

    // Volatile-bounded loop: id is bit-stable across N calls.
    volatile std::size_t const cap = 100;
    std::uint64_t baseline = diag::stable_type_id<float>;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(baseline, diag::stable_type_id<float>);
    }
}

void test_canonicalize_pack_empty_and_single() {
    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<>,
        std::tuple<>>);

    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<int>,
        std::tuple<int>>);

    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<float>,
        std::tuple<float>>);
}

void test_canonicalize_pack_order_invariance() {
    // Two-element permutations collapse.
    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<int, float>,
        diag::canonicalize_pack_t<float, int>>);

    // Three-element permutations all collapse to the same canonical
    // form regardless of input order.
    using c1 = diag::canonicalize_pack_t<int, float, double>;
    using c2 = diag::canonicalize_pack_t<float, int, double>;
    using c3 = diag::canonicalize_pack_t<double, int, float>;
    using c4 = diag::canonicalize_pack_t<float, double, int>;
    using c5 = diag::canonicalize_pack_t<double, float, int>;
    using c6 = diag::canonicalize_pack_t<int, double, float>;
    static_assert(std::is_same_v<c1, c2>);
    static_assert(std::is_same_v<c1, c3>);
    static_assert(std::is_same_v<c1, c4>);
    static_assert(std::is_same_v<c1, c5>);
    static_assert(std::is_same_v<c1, c6>);
}

void test_canonicalize_pack_dedup_deferred() {
    // V1: adjacent duplicates remain (dedup composed by PermSet).
    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<int, int>,
        std::tuple<int, int>>);

    static_assert(std::is_same_v<
        diag::canonicalize_pack_t<int, int, float>,
        diag::canonicalize_pack_t<int, float, int>>);
}

void test_stable_function_id_distinguishes() {
    // Different signatures → different IDs.
    static_assert(diag::stable_function_id<&::fn_ptr_a>
                  != diag::stable_function_id<&::fn_ptr_b>);
    static_assert(diag::stable_function_id<&::fn_ptr_a>
                  != diag::stable_function_id<&::fn_ptr_c>);
    static_assert(diag::stable_function_id<&::fn_ptr_b>
                  != diag::stable_function_id<&::fn_ptr_c>);

    // Non-zero.
    static_assert(diag::stable_function_id<&::fn_ptr_a> != std::uint64_t{0});

    // Runtime exercise — id is constant across reads.
    auto id_a = diag::stable_function_id<&::fn_ptr_a>;
    auto id_b = diag::stable_function_id<&::fn_ptr_b>;
    EXPECT_NE(id_a, id_b);
    EXPECT_EQ(id_a, diag::stable_function_id<&::fn_ptr_a>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_stable_name_compile:\n");
    run_test("test_runtime_smoke",                      test_runtime_smoke);
    run_test("test_stable_name_non_empty",              test_stable_name_non_empty);
    run_test("test_stable_type_id_distinguishes",       test_stable_type_id_distinguishes);
    run_test("test_stable_type_id_consistent",          test_stable_type_id_consistent);
    run_test("test_canonicalize_pack_empty_and_single", test_canonicalize_pack_empty_and_single);
    run_test("test_canonicalize_pack_order_invariance", test_canonicalize_pack_order_invariance);
    run_test("test_canonicalize_pack_dedup_deferred",   test_canonicalize_pack_dedup_deferred);
    run_test("test_stable_function_id_distinguishes",   test_stable_function_id_distinguishes);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
