// Two functions fuse when the first one's result can be handed straight
// to the second with the intermediate value never leaving a register.
// That promise is what the predicate has to protect, and it is why the
// admission rules are narrower than ordinary callability: the consumer
// takes exactly one argument, neither function throws or carries an
// effect, and the types match with no conversion in between.

#include <crucible/safety/Fusion.h>

#include <crucible/effects/Capabilities.h>

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

namespace safety = ::crucible::safety;
namespace effects = ::crucible::effects;

}  // namespace

namespace fusion_test {

inline int p_int_to_int(int x) noexcept { return x * 2; }
inline double p_int_to_double(int x) noexcept { return static_cast<double>(x) * 1.5; }
inline int p_double_to_int(double x) noexcept { return static_cast<int>(x); }
inline char p_int_to_char(int x) noexcept { return static_cast<char>(x % 128); }

inline int c_int_to_int(int x) noexcept { return x + 1; }
inline int c_double_to_int(double x) noexcept { return static_cast<int>(x + 0.5); }
inline double c_int_to_double(int x) noexcept { return static_cast<double>(x) / 3.0; }
inline char c_char_to_char(char x) noexcept { return static_cast<char>(x ^ 0x20); }

// Qualifiers are stripped before the types are compared, so these two
// still take int as far as the predicate is concerned.
inline int c_int_const_ref(int const& x) noexcept { return x; }
inline int c_int_rvalue_ref(int&& x) noexcept { return x; }

inline void p_void(int) noexcept {}

inline int c_nullary() noexcept { return 0; }
inline int c_binary(int, int) noexcept { return 0; }
inline int c_ternary(int, int, int) noexcept { return 0; }

// Deliberately not noexcept.
inline int p_throwing(int x) { return x; }
inline int c_throwing(int x) { return x; }

// A capability tag in the parameter list makes the function effectful.
inline int p_takes_alloc(effects::Alloc, int x) noexcept { return x; }
inline int c_takes_bg(effects::Bg, int x) noexcept { return x; }

inline int p_identity(int x) noexcept { return x; }
inline int c_identity(int x) noexcept { return x; }

// Fusion is a pairwise relation, so a three-link chain is checked one
// adjacent pair at a time rather than end to end.
inline int chain_a(int x) noexcept { return x + 1; }
inline double chain_b(int x) noexcept { return static_cast<double>(x) * 2.0; }
inline int chain_c(double x) noexcept { return static_cast<int>(x) - 1; }

}  // namespace fusion_test

namespace {

void test_positive_int_to_int_chain() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>);
    static_assert(safety::IsFusable<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>);
}

void test_positive_int_to_double_to_int() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_double, &fusion_test::c_double_to_int>);
}

void test_positive_double_to_int_to_double() {
    static_assert(safety::can_fuse_v<&fusion_test::p_double_to_int, &fusion_test::c_int_to_double>);
}

void test_positive_char_chain() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_char, &fusion_test::c_char_to_char>);
}

void test_negative_type_mismatch() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_double_to_int>);

    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_double, &fusion_test::c_int_to_double>);

    // char to int is an ordinary implicit conversion, and it is still
    // refused.  A conversion between the two calls is a step that has to
    // happen somewhere, which is exactly what fusion promises not to do.
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_char, &fusion_test::c_int_to_int>);
}

void test_negative_void_return() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_void, &fusion_test::c_int_to_int>);
}

void test_negative_nullary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_nullary>);
}

void test_negative_binary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_binary>);
}

void test_negative_ternary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_ternary>);
}

void test_negative_throwing_producer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_throwing, &fusion_test::c_int_to_int>);
}

void test_negative_throwing_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_throwing>);
}

void test_negative_impure_producer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_takes_alloc, &fusion_test::c_int_to_int>);
}

void test_negative_impure_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_takes_bg>);
}

void test_edge_identity_fusion() {
    static_assert(safety::can_fuse_v<&fusion_test::p_identity, &fusion_test::c_identity>);

    // A function fuses with itself when its return type matches its own
    // parameter type.  Nothing in the relation forbids the two sides
    // being the same function.
    static_assert(safety::can_fuse_v<&fusion_test::p_identity, &fusion_test::p_identity>);

    static_assert(safety::can_fuse_v<&fusion_test::c_identity, &fusion_test::c_identity>);
}

void test_edge_const_ref_consumer() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_int_const_ref>);
}

void test_edge_rvalue_ref_consumer() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_int_rvalue_ref>);
}

void test_edge_three_way_chain() {
    // Adjacent links fuse, and the two ends of the chain do not.  The
    // relation is not transitive, which is why a chain has to be built
    // one pair at a time.
    static_assert(safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_b>);
    static_assert(safety::can_fuse_v<&fusion_test::chain_b, &fusion_test::chain_c>);
    static_assert(!safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_c>);

    static_assert(safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_a>);
}

void test_concept_form_in_requires_clause() {
    // Only the admitting half can be witnessed from inside a compiling
    // test.  The refusing half belongs to a fixture that is expected not
    // to compile.
    auto fusable_callable = []<auto F1, auto F2>()
        requires safety::IsFusable<F1, F2>
    { return true; };

    EXPECT_TRUE((fusable_callable.template operator()<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>()));
    EXPECT_TRUE((fusable_callable.template operator()<&fusion_test::chain_a, &fusion_test::chain_b>()));
}

void test_fuse_returns_correct_value() {
    constexpr auto fused = safety::fuse<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>();

    EXPECT_TRUE(fused(7) == 15);
    EXPECT_TRUE(fused(0) == 1);
    EXPECT_TRUE(fused(-3) == -5);

    // The same expressions again at compile time, so that a fused
    // callable which only works in one of the two evaluation contexts
    // cannot pass.
    static_assert(fused(7) == 15);
    static_assert(fused(-3) == -5);
}

void test_fuse_type_changing_chain() {
    constexpr auto fused = safety::fuse<&fusion_test::p_int_to_double, &fusion_test::c_double_to_int>();

    EXPECT_TRUE(fused(10) == 15);
    EXPECT_TRUE(fused(0) == 0);

    // The fused result takes the consumer's return type, not the
    // intermediate one.
    static_assert(std::is_same_v<decltype(fused(0)), int>);
}

void test_fuse_three_way_composition() {
    constexpr auto ab = safety::fuse<&fusion_test::chain_a, &fusion_test::chain_b>();
    // This result is a double whose value happens to be exact, and
    // comparing doubles with == is refused project-wide regardless.  The
    // cast moves the comparison into integer space rather than asking
    // for an exception.
    EXPECT_TRUE(static_cast<int>(ab(0)) == 2);
    EXPECT_TRUE(static_cast<int>(ab(5)) == 12);

    constexpr auto bc = safety::fuse<&fusion_test::chain_b, &fusion_test::chain_c>();
    EXPECT_TRUE(bc(5) == 9);
    EXPECT_TRUE(bc(0) == -1);
}

void test_fuse_is_noexcept() {
    constexpr auto fused = safety::fuse<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>();
    static_assert(noexcept(fused(0)));
    static_assert(noexcept(safety::fuse<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>()));
}

void test_fuse_is_stateless_closure() {
    // The closure captures nothing, so it is as small as an object can
    // be: one byte, which exists only to give it an address.
    constexpr auto fused = safety::fuse<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>();
    static_assert(std::is_empty_v<decltype(fused)>);
}

void test_fuse_concept_gated() {
    // The generator carries the same constraint as the predicate, so
    // every pair the predicate admits must reach the generator too.
    static_assert(safety::IsFusable<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>);
    static_assert(safety::IsFusable<&fusion_test::chain_a, &fusion_test::chain_b>);
    static_assert(safety::IsFusable<&fusion_test::chain_b, &fusion_test::chain_c>);
    static_assert(!safety::IsFusable<&fusion_test::chain_a, &fusion_test::chain_c>);
}

void test_runtime_consistency() {
    // The predicate is settled at compile time, so re-reading it in a
    // loop the optimizer cannot remove is what would catch a
    // consteval-runtime divergence.
    constexpr bool baseline_pos = safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>;
    constexpr bool baseline_neg = safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_double_to_int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(!baseline_neg);

    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE((baseline_pos == safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_int_to_int>));
        EXPECT_TRUE((baseline_neg == safety::can_fuse_v<&fusion_test::p_int_to_int, &fusion_test::c_double_to_int>));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fusion:\n");
    run_test("test_positive_int_to_int_chain", test_positive_int_to_int_chain);
    run_test("test_positive_int_to_double_to_int", test_positive_int_to_double_to_int);
    run_test("test_positive_double_to_int_to_double", test_positive_double_to_int_to_double);
    run_test("test_positive_char_chain", test_positive_char_chain);
    run_test("test_negative_type_mismatch", test_negative_type_mismatch);
    run_test("test_negative_void_return", test_negative_void_return);
    run_test("test_negative_nullary_consumer", test_negative_nullary_consumer);
    run_test("test_negative_binary_consumer", test_negative_binary_consumer);
    run_test("test_negative_ternary_consumer", test_negative_ternary_consumer);
    run_test("test_negative_throwing_producer", test_negative_throwing_producer);
    run_test("test_negative_throwing_consumer", test_negative_throwing_consumer);
    run_test("test_negative_impure_producer", test_negative_impure_producer);
    run_test("test_negative_impure_consumer", test_negative_impure_consumer);
    run_test("test_edge_identity_fusion", test_edge_identity_fusion);
    run_test("test_edge_const_ref_consumer", test_edge_const_ref_consumer);
    run_test("test_edge_rvalue_ref_consumer", test_edge_rvalue_ref_consumer);
    run_test("test_edge_three_way_chain", test_edge_three_way_chain);
    run_test("test_concept_form_in_requires_clause", test_concept_form_in_requires_clause);
    run_test("test_fuse_returns_correct_value", test_fuse_returns_correct_value);
    run_test("test_fuse_type_changing_chain", test_fuse_type_changing_chain);
    run_test("test_fuse_three_way_composition", test_fuse_three_way_composition);
    run_test("test_fuse_is_noexcept", test_fuse_is_noexcept);
    run_test("test_fuse_is_stateless_closure", test_fuse_is_stateless_closure);
    run_test("test_fuse_concept_gated", test_fuse_concept_gated);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
