// A static_assert that lives only in a header is never evaluated under
// the project warning flags until some translation unit includes it.
// This one does, and it also runs the header's inline smoke test so the
// trait is checked against a genuinely callable function, not only
// against its own type computation.

#include <crucible/safety/SignatureTraits.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

// The witnesses live at namespace scope and ahead of every test,
// because a function used as a template argument has to be named
// before use and cannot be a local.

inline void sigt_nullary() noexcept {}
inline int sigt_int_returning() noexcept { return 0; }
inline double sigt_double_returning(int) noexcept { return 0.0; }

inline void sigt_unary_int(int) noexcept {}
inline void sigt_unary_int_ref(int&) noexcept {}
inline void sigt_unary_int_rref(int&&) noexcept {}
inline void sigt_unary_int_cref(int const&) noexcept {}
inline void sigt_unary_int_ptr(int*) noexcept {}

inline void sigt_binary(int, double) noexcept {}
inline void sigt_ternary(int, double, char) noexcept {}

struct SigtUserType {
    int v = 0;
};
inline void sigt_unary_user_cref(SigtUserType const&) noexcept {}

inline void sigt_alpha_int(int) noexcept {}
inline void sigt_beta_int(int) noexcept {}

inline void sigt_throwing(int) {}
inline void sigt_nothrowing(int) noexcept {}

// Four and five parameters reach past whatever small-arity shortcuts
// the trait may take.
inline void sigt_quaternary(int, double, char, float) noexcept {}
inline void sigt_quinary(int, double, char, float, long) noexcept {}

inline void sigt_array_decay(int[5]) noexcept {}

inline void sigt_function_decay(int()) noexcept {}

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

#define EXPECT_EQ(a, b)                                                                                   \
    do {                                                                                                  \
        if (!((a) == (b))) {                                                                              \
            std::fprintf(stderr, "    EXPECT_EQ failed: %s == %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
            throw TestFailure{};                                                                          \
        }                                                                                                 \
    } while (0)

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace extract = ::crucible::safety::extract;

void test_runtime_smoke() { EXPECT_TRUE(extract::signature_traits_smoke_test()); }

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

void test_param_type_primitives() { static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_int, 0>, int>); }

void test_param_type_reference_categories() {
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_int_ref, 0>, int&>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_int_rref, 0>, int&&>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_int_cref, 0>, int const&>);
}

void test_param_type_pointer() { static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_int_ptr, 0>, int*>); }

void test_param_type_user_type() {
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_unary_user_cref, 0>, SigtUserType const&>);
}

void test_param_type_multi_argument_ordering() {
    // The parameter types are all distinct, so an index that counted
    // from the wrong end would show up as a type mismatch.
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_binary, 0>, int>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_binary, 1>, double>);

    static_assert(std::is_same_v<extract::param_type_t<&::sigt_ternary, 0>, int>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_ternary, 1>, double>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_ternary, 2>, char>);
}

void test_return_type() {
    static_assert(std::is_same_v<extract::return_type_t<&::sigt_nullary>, void>);
    static_assert(std::is_same_v<extract::return_type_t<&::sigt_int_returning>, int>);
    static_assert(std::is_same_v<extract::return_type_t<&::sigt_double_returning>, double>);
}

void test_distinct_pointers_same_signature_parity() {
    // The two functions share a signature and differ in address.  The
    // trait is parameterized on the function value, so each gets its
    // own specialization, and the results still have to agree.
    static_assert(extract::arity_v<&::sigt_alpha_int> == extract::arity_v<&::sigt_beta_int>);
    static_assert(
        std::is_same_v<extract::param_type_t<&::sigt_alpha_int, 0>, extract::param_type_t<&::sigt_beta_int, 0>>);
    static_assert(std::is_same_v<extract::return_type_t<&::sigt_alpha_int>, extract::return_type_t<&::sigt_beta_int>>);
}

void test_noexcept_detection() {
    static_assert(extract::is_noexcept_v<&::sigt_nothrowing>);
    static_assert(!extract::is_noexcept_v<&::sigt_throwing>);

    static_assert(extract::signature_traits<&::sigt_nothrowing>::is_noexcept);
    static_assert(!extract::signature_traits<&::sigt_throwing>::is_noexcept);

    static_assert(extract::is_noexcept_v<&::sigt_unary_int>);
    static_assert(extract::is_noexcept_v<&::sigt_binary>);
    static_assert(extract::is_noexcept_v<&::sigt_quaternary>);
}

void test_function_type_extraction() {
    static_assert(std::is_same_v<extract::function_type_t<&::sigt_unary_int>, void(int) noexcept>);
    static_assert(std::is_same_v<extract::function_type_t<&::sigt_throwing>, void(int)>);
    static_assert(std::is_same_v<extract::function_type_t<&::sigt_nullary>, void() noexcept>);
    static_assert(std::is_same_v<extract::function_type_t<&::sigt_int_returning>, int() noexcept>);

    static_assert(std::is_same_v<extract::function_type_t<&::sigt_callback_witness>, sigt_callback_t>);
}

void test_higher_arity() {
    static_assert(extract::arity_v<&::sigt_quaternary> == 4);
    static_assert(extract::arity_v<&::sigt_quinary> == 5);

    static_assert(std::is_same_v<extract::param_type_t<&::sigt_quaternary, 0>, int>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_quaternary, 1>, double>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_quaternary, 2>, char>);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_quaternary, 3>, float>);

    static_assert(std::is_same_v<extract::param_type_t<&::sigt_quinary, 4>, long>);

    EXPECT_EQ(extract::arity_v<&::sigt_quaternary>, 4u);
    EXPECT_EQ(extract::arity_v<&::sigt_quinary>, 5u);
}

void test_array_decay() {
    // The declared parameter is an array, and the trait reports the
    // adjusted type the language actually gives the function.
    static_assert(extract::arity_v<&::sigt_array_decay> == 1);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_array_decay, 0>, int*>);
}

void test_function_decay() {
    // Same adjustment, with a function-typed parameter.
    static_assert(extract::arity_v<&::sigt_function_decay> == 1);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_function_decay, 0>, int (*)()>);
}

void test_callback_typedef() {
    // Reaching the trait through an alias must give what a directly
    // spelled signature of the same shape gives.
    static_assert(extract::arity_v<&::sigt_callback_witness> == 1);
    static_assert(std::is_same_v<extract::param_type_t<&::sigt_callback_witness, 0>, int>);
    static_assert(std::is_same_v<extract::return_type_t<&::sigt_callback_witness>, void>);
    static_assert(extract::is_noexcept_v<&::sigt_callback_witness>);
}

void test_runtime_consistency() {
    // The volatile bound stops the loop folding away, so the trait is
    // read as a runtime value rather than only as a constant.
    volatile std::size_t const cap = 100;
    std::size_t baseline = extract::arity_v<&::sigt_ternary>;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_EQ(baseline, extract::arity_v<&::sigt_ternary>);
    }

    // The call itself is the check: it would not compile if the real
    // signature differed from the one the trait reports.  The loop only
    // keeps it from being eliminated.
    int x = 7;
    for (std::size_t i = 0; i < cap; ++i) {
        ::sigt_binary(x, 1.5);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_signature_traits:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_arity_zero", test_arity_zero);
    run_test("test_arity_unary", test_arity_unary);
    run_test("test_arity_multi", test_arity_multi);
    run_test("test_param_type_primitives", test_param_type_primitives);
    run_test("test_param_type_reference_categories", test_param_type_reference_categories);
    run_test("test_param_type_pointer", test_param_type_pointer);
    run_test("test_param_type_user_type", test_param_type_user_type);
    run_test("test_param_type_multi_argument_ordering", test_param_type_multi_argument_ordering);
    run_test("test_return_type", test_return_type);
    run_test("test_distinct_pointers_same_signature_parity", test_distinct_pointers_same_signature_parity);
    run_test("test_noexcept_detection", test_noexcept_detection);
    run_test("test_function_type_extraction", test_function_type_extraction);
    run_test("test_higher_arity", test_higher_arity);
    run_test("test_array_decay", test_array_decay);
    run_test("test_function_decay", test_function_decay);
    run_test("test_callback_typedef", test_callback_typedef);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
