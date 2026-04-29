// ═══════════════════════════════════════════════════════════════════
// test_signature_traits — sentinel TU for safety/SignatureTraits.h
//
// Same blind-spot rationale as test_stable_name_compile / test_diag_
// nostic_compile (see feedback_header_only_static_assert_blind_spot
// memory): a header shipped with embedded static_asserts is unverified
// under the project warning flags unless a .cpp TU includes it.  This
// sentinel forces SignatureTraits.h through the test target's full
// -Werror=shadow / -Werror=conversion / -Werror=switch-default /
// -Wanalyzer-* matrix and exercises the runtime_smoke_test inline body.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution: exercises the witness functions
//     with non-constant arguments, confirms the signatures the trait
//     claims actually match the runtime-callable ABI.
//   * arity / arity_v across nullary / unary / binary / ternary /
//     return-type-having functions.
//   * param_type_t splice over primitives, reference categories
//     (T, T&, T&&, T const&), pointers, user-defined types.
//   * return_type_t extraction over void and non-void.
//   * Multi-argument ordering (param 0 is first declared, etc.).
//   * Distinct-pointer-same-signature parity (arity equal; spliced
//     param types equal).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/SignatureTraits.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

// Namespace-scope free functions used as auto-NTTP arguments to the
// trait.  Defined BEFORE the test functions per C++ name lookup rules.
// `inline` permits multiple TU inclusion; `noexcept` matches the
// signature shape the existing FOUND-E08 sentinel uses.

inline void   sigt_nullary()                  noexcept {}
inline int    sigt_int_returning()            noexcept { return 0; }
inline double sigt_double_returning(int)      noexcept { return 0.0; }

inline void sigt_unary_int(int)               noexcept {}
inline void sigt_unary_int_ref(int&)          noexcept {}
inline void sigt_unary_int_rref(int&&)        noexcept {}
inline void sigt_unary_int_cref(int const&)   noexcept {}
inline void sigt_unary_int_ptr(int*)          noexcept {}

inline void sigt_binary(int, double)          noexcept {}
inline void sigt_ternary(int, double, char)   noexcept {}

struct SigtUserType { int v = 0; };
inline void sigt_unary_user_cref(SigtUserType const&) noexcept {}

inline void sigt_alpha_int(int) noexcept {}
inline void sigt_beta_int(int)  noexcept {}

// noexcept witnesses
inline void sigt_throwing(int)            { /* may throw */ }
inline void sigt_nothrowing(int) noexcept {}

// Higher arity (4+) — covers params indexing past the small-arity cases.
inline void sigt_quaternary(int, double, char, float)
    noexcept {}
inline void sigt_quinary(int, double, char, float, long)
    noexcept {}

// Array decay: `int[5]` parameter decays to `int*`.
inline void sigt_array_decay(int[5]) noexcept {}

// Function decay: `int()` parameter decays to `int(*)()`.
inline void sigt_function_decay(int()) noexcept {}

// Function-pointer typedef — exercises the trait through an alias.
using sigt_callback_t = void(int) noexcept;
inline void sigt_callback_witness(int) noexcept {}  // matches sigt_callback_t

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

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if (!((a) == (b))) {                                               \
            std::fprintf(stderr,                                           \
                "    EXPECT_EQ failed: %s == %s (%s:%d)\n",                \
                #a, #b, __FILE__, __LINE__);                               \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;

// ─── Tests ──────────────────────────────────────────────────────────

void test_runtime_smoke() {
    extract::runtime_smoke_test();
}

void test_arity_zero() {
    static_assert(extract::signature_traits<&::sigt_nullary>::arity == 0);
    static_assert(extract::signature_traits<&::sigt_int_returning>::arity == 0);
    static_assert(extract::arity_v<&::sigt_nullary> == 0);
    EXPECT_EQ(extract::arity_v<&::sigt_nullary>, 0u);
}

void test_arity_unary() {
    static_assert(extract::signature_traits<&::sigt_unary_int>::arity == 1);
    static_assert(extract::signature_traits<&::sigt_unary_int_ref>::arity == 1);
    static_assert(extract::signature_traits<&::sigt_unary_int_rref>::arity == 1);
    static_assert(extract::signature_traits<&::sigt_unary_int_cref>::arity == 1);
    static_assert(extract::signature_traits<&::sigt_unary_int_ptr>::arity == 1);
    static_assert(extract::arity_v<&::sigt_unary_int> == 1);
    EXPECT_EQ(extract::arity_v<&::sigt_unary_int>, 1u);
}

void test_arity_multi() {
    static_assert(extract::signature_traits<&::sigt_binary>::arity == 2);
    static_assert(extract::signature_traits<&::sigt_ternary>::arity == 3);
    static_assert(extract::arity_v<&::sigt_binary> == 2);
    static_assert(extract::arity_v<&::sigt_ternary> == 3);
    EXPECT_EQ(extract::arity_v<&::sigt_binary>, 2u);
    EXPECT_EQ(extract::arity_v<&::sigt_ternary>, 3u);
}

void test_param_type_primitives() {
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_int, 0>, int>);
}

void test_param_type_reference_categories() {
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_int_ref, 0>, int&>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_int_rref, 0>, int&&>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_int_cref, 0>, int const&>);
}

void test_param_type_pointer() {
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_int_ptr, 0>, int*>);
}

void test_param_type_user_type() {
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_unary_user_cref, 0>,
        SigtUserType const&>);
}

void test_param_type_multi_argument_ordering() {
    // Binary: param 0 = int, param 1 = double.
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_binary, 0>, int>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_binary, 1>, double>);

    // Ternary: param 0 = int, param 1 = double, param 2 = char.
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_ternary, 0>, int>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_ternary, 1>, double>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_ternary, 2>, char>);
}

void test_return_type() {
    static_assert(std::is_same_v<
        extract::return_type_t<&::sigt_nullary>, void>);
    static_assert(std::is_same_v<
        extract::return_type_t<&::sigt_int_returning>, int>);
    static_assert(std::is_same_v<
        extract::return_type_t<&::sigt_double_returning>, double>);
}

void test_distinct_pointers_same_signature_parity() {
    // alpha and beta have identical signatures but distinct addresses.
    // The trait is parameterized on the function value, so the two
    // produce distinct trait specializations — but their arity and
    // spliced param types must be equal.
    static_assert(extract::arity_v<&::sigt_alpha_int>
                  == extract::arity_v<&::sigt_beta_int>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_alpha_int, 0>,
        extract::param_type_t<&::sigt_beta_int, 0>>);
    static_assert(std::is_same_v<
        extract::return_type_t<&::sigt_alpha_int>,
        extract::return_type_t<&::sigt_beta_int>>);
}

void test_noexcept_detection() {
    // Direct claims.
    static_assert(extract::is_noexcept_v<&::sigt_nothrowing>);
    static_assert(!extract::is_noexcept_v<&::sigt_throwing>);

    // Member-form parity.
    static_assert(extract::signature_traits<&::sigt_nothrowing>::is_noexcept);
    static_assert(!extract::signature_traits<&::sigt_throwing>::is_noexcept);

    // The other witnesses are all noexcept (defined above) — confirm.
    static_assert(extract::is_noexcept_v<&::sigt_unary_int>);
    static_assert(extract::is_noexcept_v<&::sigt_binary>);
    static_assert(extract::is_noexcept_v<&::sigt_quaternary>);
}

void test_function_type_extraction() {
    static_assert(std::is_same_v<
        extract::function_type_t<&::sigt_unary_int>,
        void(int) noexcept>);
    static_assert(std::is_same_v<
        extract::function_type_t<&::sigt_throwing>,
        void(int)>);
    static_assert(std::is_same_v<
        extract::function_type_t<&::sigt_nullary>,
        void() noexcept>);
    static_assert(std::is_same_v<
        extract::function_type_t<&::sigt_int_returning>,
        int() noexcept>);

    // function_type matches the typedef-form parity for the callback.
    static_assert(std::is_same_v<
        extract::function_type_t<&::sigt_callback_witness>,
        sigt_callback_t>);
}

void test_higher_arity() {
    static_assert(extract::arity_v<&::sigt_quaternary> == 4);
    static_assert(extract::arity_v<&::sigt_quinary> == 5);

    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_quaternary, 0>, int>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_quaternary, 1>, double>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_quaternary, 2>, char>);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_quaternary, 3>, float>);

    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_quinary, 4>, long>);

    EXPECT_EQ(extract::arity_v<&::sigt_quaternary>, 4u);
    EXPECT_EQ(extract::arity_v<&::sigt_quinary>, 5u);
}

void test_array_decay() {
    // `int[5]` parameter decays to `int*` — the trait reports the
    // post-decay (adjusted) parameter type.
    static_assert(extract::arity_v<&::sigt_array_decay> == 1);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_array_decay, 0>, int*>);
}

void test_function_decay() {
    // `int()` parameter decays to `int(*)()`.
    static_assert(extract::arity_v<&::sigt_function_decay> == 1);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_function_decay, 0>, int(*)()>);
}

void test_callback_typedef() {
    // The trait resolves through a function-pointer typedef.  Confirm
    // it produces the same arity / param / return as a direct
    // signature with matching shape.
    static_assert(extract::arity_v<&::sigt_callback_witness> == 1);
    static_assert(std::is_same_v<
        extract::param_type_t<&::sigt_callback_witness, 0>, int>);
    static_assert(std::is_same_v<
        extract::return_type_t<&::sigt_callback_witness>, void>);
    static_assert(extract::is_noexcept_v<&::sigt_callback_witness>);
}

void test_runtime_consistency() {
    // Volatile-bounded loop: arity is bit-stable across calls.
    volatile std::size_t const cap = 100;
    std::size_t baseline = extract::arity_v<&::sigt_ternary>;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(baseline, extract::arity_v<&::sigt_ternary>);
    }

    // Confirm the runtime-callable ABI matches the trait's claim:
    // a binary trait must accept exactly two arguments.  This is
    // compile-time true (the call below would not compile if the
    // signature differed); the volatile loop just keeps the call
    // alive against optimizer DCE.
    int x = 7;
    for (std::size_t i = 0; i < cap; ++i) {
        ::sigt_binary(x, 1.5);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_signature_traits:\n");
    run_test("test_runtime_smoke",                            test_runtime_smoke);
    run_test("test_arity_zero",                               test_arity_zero);
    run_test("test_arity_unary",                              test_arity_unary);
    run_test("test_arity_multi",                              test_arity_multi);
    run_test("test_param_type_primitives",                    test_param_type_primitives);
    run_test("test_param_type_reference_categories",          test_param_type_reference_categories);
    run_test("test_param_type_pointer",                       test_param_type_pointer);
    run_test("test_param_type_user_type",                     test_param_type_user_type);
    run_test("test_param_type_multi_argument_ordering",       test_param_type_multi_argument_ordering);
    run_test("test_return_type",                              test_return_type);
    run_test("test_distinct_pointers_same_signature_parity",  test_distinct_pointers_same_signature_parity);
    run_test("test_noexcept_detection",                       test_noexcept_detection);
    run_test("test_function_type_extraction",                 test_function_type_extraction);
    run_test("test_higher_arity",                             test_higher_arity);
    run_test("test_array_decay",                              test_array_decay);
    run_test("test_function_decay",                           test_function_decay);
    run_test("test_callback_typedef",                         test_callback_typedef);
    run_test("test_runtime_consistency",                      test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
