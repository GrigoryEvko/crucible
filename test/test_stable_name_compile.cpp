// A header that ships its own static_asserts is never checked against the
// project's warning matrix until some translation unit includes it. This one
// exists to be that translation unit, and to run the header's inline smoke
// body with non-constant arguments.

#include <crucible/safety/diag/_StableName.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

// Defined at namespace scope and ahead of their use, so a function named as an
// auto NTTP has the linkage the deduction needs.

inline void fn_ptr_a() noexcept {}
inline void fn_ptr_b(int) noexcept {}
inline int fn_ptr_c(int, float) noexcept { return 0; }

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

#define EXPECT_NE(a, b)                                                                                   \
    do {                                                                                                  \
        if (!((a) != (b))) {                                                                              \
            std::fprintf(stderr, "    EXPECT_NE failed: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
            throw TestFailure{};                                                                          \
        }                                                                                                 \
    } while (0)

namespace diag = ::crucible::safety::diag;

void test_runtime_smoke() { diag::runtime_smoke_test_stable_name(); }

void test_stable_name_non_empty() {
    EXPECT_TRUE(!diag::stable_name_of<int>.empty());
    EXPECT_TRUE(!diag::stable_name_of<float>.empty());
    EXPECT_TRUE(!diag::stable_name_of<double>.empty());
    EXPECT_TRUE(!diag::stable_name_of<void>.empty());
    EXPECT_TRUE(!diag::stable_name_of<std::string_view>.empty());

    // The leading part of a captured spelling depends on the context that
    // captures it, so only the suffix can be matched.
    EXPECT_TRUE(diag::stable_name_of<int>.ends_with("int"));
    EXPECT_TRUE(diag::stable_name_of<float>.ends_with("float"));
    EXPECT_TRUE(diag::stable_name_of<void>.ends_with("void"));
}

void test_stable_type_id_distinguishes() {
    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<float>);
    EXPECT_NE(diag::stable_type_id<float>, diag::stable_type_id<double>);
    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<long>);
    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<unsigned int>);
    EXPECT_NE(diag::stable_type_id<char>, diag::stable_type_id<unsigned char>);
    EXPECT_NE(diag::stable_type_id<short>, diag::stable_type_id<int>);
    EXPECT_NE(diag::stable_type_id<void>, diag::stable_type_id<int>);

    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<int*>);
    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<int const>);
    EXPECT_NE(diag::stable_type_id<int>, diag::stable_type_id<int&>);

    EXPECT_NE(diag::stable_type_id<int>, std::uint64_t{0});
    EXPECT_NE(diag::stable_type_id<void>, std::uint64_t{0});
}

void test_stable_type_id_consistent() {
    auto id1 = diag::stable_type_id<int>;
    auto id2 = diag::stable_type_id<int>;
    EXPECT_EQ(id1, id2);

    // The bound is volatile so the loop survives, and the identifier is read
    // outside a constant-evaluated context.
    volatile std::size_t const cap = 100;
    std::uint64_t baseline = diag::stable_type_id<float>;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(baseline, diag::stable_type_id<float>);
    }
}

// These literals repeat pins the header already makes at compile time. The
// duplication is deliberate: a constant-folder bypass can let the compile-time
// assertion see one value while the emitted code carries another, and only a
// runtime read catches that. Both layers must agree.
//
// A failure here means the canonicalization of the captured spelling changed.
// The literals below and the pins in the header are refreshed together, never
// one alone.

void test_stable_type_id_pinned_v1_bit_stability() {
    EXPECT_EQ(diag::stable_type_id<int>, 0x038bf5d93760ba14ULL);
    EXPECT_EQ(diag::stable_type_id<unsigned int>, 0x3e40352bf14d5e8cULL);
    EXPECT_EQ(diag::stable_type_id<float>, 0xaac94173610ce8ebULL);
    EXPECT_EQ(diag::stable_type_id<double>, 0x5a427827acb3b7f4ULL);
    EXPECT_EQ(diag::stable_type_id<void>, 0x7095b61429cf52a0ULL);
    EXPECT_EQ(diag::stable_type_id<char>, 0x24810aa534fd4e53ULL);
    EXPECT_EQ(diag::stable_type_id<unsigned char>, 0xeb532a1cd85a3221ULL);
    EXPECT_EQ(diag::stable_type_id<signed char>, 0xe668b88a72723d2eULL);
    EXPECT_EQ(diag::stable_type_id<short>, 0x76a26fe7af41346dULL);
    EXPECT_EQ(diag::stable_type_id<long>, 0xb398537731c4a05dULL);
    EXPECT_EQ(diag::stable_type_id<long long>, 0x8e73a318de406be0ULL);
    EXPECT_EQ(diag::stable_type_id<unsigned long long>, 0xcb9dc82adf69491aULL);
    EXPECT_EQ(diag::stable_type_id<bool>, 0xc7dfd75159543180ULL);
}

void test_canonicalize_pack_empty_and_single() {
    static_assert(std::is_same_v<diag::canonicalize_pack_t<>, std::tuple<>>);

    static_assert(std::is_same_v<diag::canonicalize_pack_t<int>, std::tuple<int>>);

    static_assert(std::is_same_v<diag::canonicalize_pack_t<float>, std::tuple<float>>);
}

void test_canonicalize_pack_order_invariance() {
    static_assert(std::is_same_v<diag::canonicalize_pack_t<int, float>, diag::canonicalize_pack_t<float, int>>);

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
    // Duplicates survive canonicalization. Removing them belongs to the layer
    // that composes packs, not to the canonical ordering.
    static_assert(std::is_same_v<diag::canonicalize_pack_t<int, int>, std::tuple<int, int>>);

    static_assert(
        std::is_same_v<diag::canonicalize_pack_t<int, int, float>, diag::canonicalize_pack_t<int, float, int>>);
}

void test_stable_function_id_distinguishes() {
    static_assert(diag::stable_function_id<&::fn_ptr_a> != diag::stable_function_id<&::fn_ptr_b>);
    static_assert(diag::stable_function_id<&::fn_ptr_a> != diag::stable_function_id<&::fn_ptr_c>);
    static_assert(diag::stable_function_id<&::fn_ptr_b> != diag::stable_function_id<&::fn_ptr_c>);

    static_assert(diag::stable_function_id<&::fn_ptr_a> != std::uint64_t{0});

    auto id_a = diag::stable_function_id<&::fn_ptr_a>;
    auto id_b = diag::stable_function_id<&::fn_ptr_b>;
    EXPECT_NE(id_a, id_b);
    EXPECT_EQ(id_a, diag::stable_function_id<&::fn_ptr_a>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_stable_name_compile:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_stable_name_non_empty", test_stable_name_non_empty);
    run_test("test_stable_type_id_distinguishes", test_stable_type_id_distinguishes);
    run_test("test_stable_type_id_consistent", test_stable_type_id_consistent);
    run_test("test_stable_type_id_pinned_v1_bit_stability", test_stable_type_id_pinned_v1_bit_stability);
    run_test("test_canonicalize_pack_empty_and_single", test_canonicalize_pack_empty_and_single);
    run_test("test_canonicalize_pack_order_invariance", test_canonicalize_pack_order_invariance);
    run_test("test_canonicalize_pack_dedup_deferred", test_canonicalize_pack_dedup_deferred);
    run_test("test_stable_function_id_distinguishes", test_stable_function_id_distinguishes);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
