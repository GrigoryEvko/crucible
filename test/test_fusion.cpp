// ═══════════════════════════════════════════════════════════════════
// test_fusion — sentinel TU for safety/Fusion.h
//
// FOUND-F06 — exercises the can_fuse_v / IsFusable predicate with
// the full positive + negative matrix:
//
//   Positive: matching producer→consumer chain
//   Negative: type mismatch
//   Negative: void-returning producer
//   Negative: arity mismatch (nullary, binary, ternary consumer)
//   Negative: throwing producer / consumer
//   Negative: cap-tag in producer / consumer (impure)
//   Edge:     identity-fusable chain (T → T → T)
//   Edge:     reference / const-qualified consumer parameter
//   Edge:     three-way chain (composable in pairs)
// ═══════════════════════════════════════════════════════════════════

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace safety  = ::crucible::safety;
namespace effects = ::crucible::effects;

}  // namespace

namespace fusion_test {

// ── Catalog of producer/consumer signatures ───────────────────────

inline int    p_int_to_int      (int x)    noexcept { return x * 2; }
inline double p_int_to_double   (int x)    noexcept { return static_cast<double>(x) * 1.5; }
inline int    p_double_to_int   (double x) noexcept { return static_cast<int>(x); }
inline char   p_int_to_char     (int x)    noexcept { return static_cast<char>(x % 128); }

inline int    c_int_to_int      (int x)    noexcept { return x + 1; }
inline int    c_double_to_int   (double x) noexcept { return static_cast<int>(x + 0.5); }
inline double c_int_to_double   (int x)    noexcept { return static_cast<double>(x) / 3.0; }
inline char   c_char_to_char    (char x)   noexcept { return static_cast<char>(x ^ 0x20); }

// Edge: const / ref-qualified consumer parameter — V1 strips cv-ref
// before composability check.
inline int    c_int_const_ref       (int const&  x) noexcept { return x; }
inline int    c_int_rvalue_ref      (int&&       x) noexcept { return x; }

// Negative: void return.
inline void   p_void(int) noexcept {}

// Negative: nullary / binary / ternary consumer.
inline int    c_nullary  ()             noexcept { return 0; }
inline int    c_binary   (int, int)     noexcept { return 0; }
inline int    c_ternary  (int, int, int) noexcept { return 0; }

// Negative: throwing producer / consumer.
inline int    p_throwing (int x) { return x; }      // not noexcept
inline int    c_throwing (int x) { return x; }      // not noexcept

// Negative: impure (cap-tag in parameter list).
inline int    p_takes_alloc(effects::Alloc, int x) noexcept { return x; }
inline int    c_takes_bg   (effects::Bg, int x)    noexcept { return x; }

// Edge: identity fusion T → T → T.
inline int    p_identity (int x) noexcept { return x; }
inline int    c_identity (int x) noexcept { return x; }

// Edge: three-way chain (a ∘ b ∘ c). V1 fuses pairwise; this is
// ad-hoc transitive fusibility verified by separate pair-checks.
inline int    chain_a (int x)    noexcept { return x + 1; }
inline double chain_b (int x)    noexcept { return static_cast<double>(x) * 2.0; }
inline int    chain_c (double x) noexcept { return static_cast<int>(x) - 1; }

}  // namespace fusion_test

namespace {

// ═════════════════════════════════════════════════════════════════════
// ── Positive — matching producer-consumer chains ──────────────────
// ═════════════════════════════════════════════════════════════════════

void test_positive_int_to_int_chain() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int,
                                     &fusion_test::c_int_to_int>);
    static_assert(safety::IsFusable<&fusion_test::p_int_to_int,
                                    &fusion_test::c_int_to_int>);
}

void test_positive_int_to_double_to_int() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_double,
                                     &fusion_test::c_double_to_int>);
}

void test_positive_double_to_int_to_double() {
    static_assert(safety::can_fuse_v<&fusion_test::p_double_to_int,
                                     &fusion_test::c_int_to_double>);
}

void test_positive_char_chain() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_char,
                                     &fusion_test::c_char_to_char>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Negative — type mismatch ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_negative_type_mismatch() {
    // p_int_to_int returns int; c_double_to_int takes double.
    // Even though both arities are 1, the types don't compose.
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_double_to_int>);

    // p_int_to_double returns double; c_int_to_double takes int.
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_double,
                                      &fusion_test::c_int_to_double>);

    // p_int_to_char returns char; c_int_to_int takes int.
    // This deserves attention: char→int is a valid implicit conversion
    // in plain C++, but V1 deliberately rejects to keep fusion's
    // "intermediate-in-registers" promise (no hidden conversions).
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_char,
                                      &fusion_test::c_int_to_int>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Negative — void return ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_negative_void_return() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_void,
                                      &fusion_test::c_int_to_int>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Negative — arity mismatch (V1 requires unary consumer) ────────
// ═════════════════════════════════════════════════════════════════════

void test_negative_nullary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_nullary>);
}

void test_negative_binary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_binary>);
}

void test_negative_ternary_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_ternary>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Negative — throwing function ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_negative_throwing_producer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_throwing,
                                      &fusion_test::c_int_to_int>);
}

void test_negative_throwing_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_throwing>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Negative — impure (cap-tag in parameter list) ─────────────────
// ═════════════════════════════════════════════════════════════════════

void test_negative_impure_producer() {
    // p_takes_alloc takes (effects::Alloc, int) — non-empty inferred
    // row.  Fusion must reject impure producers per V1 contract.
    static_assert(!safety::can_fuse_v<&fusion_test::p_takes_alloc,
                                      &fusion_test::c_int_to_int>);
}

void test_negative_impure_consumer() {
    static_assert(!safety::can_fuse_v<&fusion_test::p_int_to_int,
                                      &fusion_test::c_takes_bg>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Edge — identity fusion (T → T → T) ────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_edge_identity_fusion() {
    static_assert(safety::can_fuse_v<&fusion_test::p_identity,
                                     &fusion_test::c_identity>);

    // Self-fusion: a function fuses with itself iff its return type
    // matches its single parameter type.
    static_assert(safety::can_fuse_v<&fusion_test::p_identity,
                                     &fusion_test::p_identity>);

    // Mixed self-fusion: c_identity composes with itself.
    static_assert(safety::can_fuse_v<&fusion_test::c_identity,
                                     &fusion_test::c_identity>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Edge — cv-ref-qualified consumer parameter ────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_edge_const_ref_consumer() {
    // V1 strips cv-ref via remove_cvref_t before the composability
    // check.  c_int_const_ref's parameter is `int const&`; after
    // strip it's `int`, matching p_int_to_int's return.
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int,
                                     &fusion_test::c_int_const_ref>);
}

void test_edge_rvalue_ref_consumer() {
    static_assert(safety::can_fuse_v<&fusion_test::p_int_to_int,
                                     &fusion_test::c_int_rvalue_ref>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Edge — three-way chain composability ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_edge_three_way_chain() {
    // chain_a: int → int
    // chain_b: int → double
    // chain_c: double → int
    //
    // Pairwise fusibility:
    //   (a, b) : int return → int param → fusable
    //   (b, c) : double return → double param → fusable
    //   (a, c) : int return → double param → NOT fusable
    static_assert( safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_b>);
    static_assert( safety::can_fuse_v<&fusion_test::chain_b, &fusion_test::chain_c>);
    static_assert(!safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_c>);

    // Cycle: chain_a is int→int, so a∘a is fusable.
    static_assert( safety::can_fuse_v<&fusion_test::chain_a, &fusion_test::chain_a>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Concept gate in requires-clause ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_concept_form_in_requires_clause() {
    // A template constrained on IsFusable is admitted only for
    // fusable pairs.  Verify by instantiating successfully on a
    // fusable pair and observing the rejection path elsewhere
    // (which is the neg-compile fixture's job).
    auto fusable_callable = []<auto F1, auto F2>()
        requires safety::IsFusable<F1, F2>
    {
        return true;
    };

    EXPECT_TRUE((fusable_callable.template operator()<
        &fusion_test::p_int_to_int, &fusion_test::c_int_to_int>()));
    EXPECT_TRUE((fusable_callable.template operator()<
        &fusion_test::chain_a, &fusion_test::chain_b>()));
}

void test_runtime_consistency() {
    // can_fuse_v is a pure compile-time predicate.  The runtime check
    // verifies the 50-iteration consistency under volatile-anchored
    // cap, mirroring the discipline from the wrapper-detector tests.
    constexpr bool baseline_pos = safety::can_fuse_v<&fusion_test::p_int_to_int,
                                                     &fusion_test::c_int_to_int>;
    constexpr bool baseline_neg = safety::can_fuse_v<&fusion_test::p_int_to_int,
                                                     &fusion_test::c_double_to_int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(!baseline_neg);

    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE((baseline_pos == safety::can_fuse_v<
            &fusion_test::p_int_to_int, &fusion_test::c_int_to_int>));
        EXPECT_TRUE((baseline_neg == safety::can_fuse_v<
            &fusion_test::p_int_to_int, &fusion_test::c_double_to_int>));
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fusion:\n");
    run_test("test_positive_int_to_int_chain",       test_positive_int_to_int_chain);
    run_test("test_positive_int_to_double_to_int",   test_positive_int_to_double_to_int);
    run_test("test_positive_double_to_int_to_double",test_positive_double_to_int_to_double);
    run_test("test_positive_char_chain",             test_positive_char_chain);
    run_test("test_negative_type_mismatch",          test_negative_type_mismatch);
    run_test("test_negative_void_return",            test_negative_void_return);
    run_test("test_negative_nullary_consumer",       test_negative_nullary_consumer);
    run_test("test_negative_binary_consumer",        test_negative_binary_consumer);
    run_test("test_negative_ternary_consumer",       test_negative_ternary_consumer);
    run_test("test_negative_throwing_producer",      test_negative_throwing_producer);
    run_test("test_negative_throwing_consumer",      test_negative_throwing_consumer);
    run_test("test_negative_impure_producer",        test_negative_impure_producer);
    run_test("test_negative_impure_consumer",        test_negative_impure_consumer);
    run_test("test_edge_identity_fusion",            test_edge_identity_fusion);
    run_test("test_edge_const_ref_consumer",         test_edge_const_ref_consumer);
    run_test("test_edge_rvalue_ref_consumer",        test_edge_rvalue_ref_consumer);
    run_test("test_edge_three_way_chain",            test_edge_three_way_chain);
    run_test("test_concept_form_in_requires_clause", test_concept_form_in_requires_clause);
    run_test("test_runtime_consistency",             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
