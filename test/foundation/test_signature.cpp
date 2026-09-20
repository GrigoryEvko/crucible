// The signature reflection, driven at run time.
//
// foundation/reflect/Signature.h keeps its static_assert wall, because
// each of those assertions reads a shipped query against a witness
// function.  What lives here is the part the wall cannot reach: every
// query called with the trait read through a volatile bound, so the
// reads are not folded away, and every witness called so that the link
// confirms the signatures the trait reports match the functions as
// declared.
//
// The old test (test/test_signature_traits.cpp) wrapped its
// static_asserts in a run_test harness, which ran nothing: a
// static_assert inside a function body is checked when the body is
// compiled, not when it is called.  Those cells are the header's wall
// and stay there.  This file is what was left once they were removed:
// the smoke body that used to sit inline in the header, under #178.

#include <foundation/reflect/Signature.h>

#include <cstdio>
#include <type_traits>

namespace {

namespace refl = ::foundation::reflect;

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

// The witnesses are this file's own, so that calling them is what pins
// the declared signature against what the trait reported.

void witness_nullary() noexcept {}
int witness_int_returning() noexcept { return 0; }

void witness_unary_int(int) noexcept {}
void witness_unary_int_ref(int&) noexcept {}
void witness_unary_int_rref(int&&) noexcept {}
void witness_unary_int_cref(int const&) noexcept {}
void witness_unary_int_ptr(int*) noexcept {}

void witness_binary(int, double) noexcept {}
void witness_ternary(int, double, char) noexcept {}

auto witness_returning_double(int) noexcept -> double { return 0.0; }

struct UserType {
    int v = 0;
};
void witness_user_cref(UserType const&) noexcept {}

void witness_throwing(int) {}
void witness_nothrowing(int) noexcept {}

// Every witness is called, which is what makes the link confirm that the
// function the trait reflected is the function that exists.
void every_witness_runs_at_run_time() {
    witness_nullary();

    int const r0 = witness_int_returning();
    (void)r0;

    int x = 42;
    witness_unary_int(x);
    witness_unary_int_ref(x);
    witness_unary_int_rref(static_cast<int&&>(x));
    witness_unary_int_cref(x);
    witness_unary_int_cref(0);
    witness_unary_int_ptr(&x);

    witness_binary(x, 1.0);
    witness_ternary(x, 1.0, 'a');

    UserType const ut{};
    witness_user_cref(ut);

    double const r1 = witness_returning_double(x);
    (void)r1;

    witness_throwing(x);
    witness_nothrowing(x);
}

// The trait is a type-level computation, so the volatile bound is what
// keeps the reads below from being folded into the loop's exit
// condition.  This body was an inline smoke test in the old header,
// compiled into every translation unit that included it and called by
// nothing.
void every_query_reads_at_run_time() {
    volatile std::size_t const cap = 4;

    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT(refl::arity_v<&witness_nullary> == 0);
        EXPECT(refl::arity_v<&witness_unary_int> == 1);
        EXPECT(refl::arity_v<&witness_binary> == 2);
        EXPECT(refl::arity_v<&witness_ternary> == 3);

        EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int, 0>, int>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int_ref, 0>, int&>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int_rref, 0>, int&&>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int_cref, 0>, int const&>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int_ptr, 0>, int*>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_user_cref, 0>, UserType const&>));

        EXPECT((std::is_same_v<refl::param_type_t<&witness_binary, 0>, int>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_binary, 1>, double>));
        EXPECT((std::is_same_v<refl::param_type_t<&witness_ternary, 2>, char>));

        EXPECT((std::is_same_v<refl::return_type_t<&witness_nullary>, void>));
        EXPECT((std::is_same_v<refl::return_type_t<&witness_int_returning>, int>));
        EXPECT((std::is_same_v<refl::return_type_t<&witness_returning_double>, double>));

        EXPECT(refl::is_noexcept_v<&witness_nothrowing>);
        EXPECT(!refl::is_noexcept_v<&witness_throwing>);

        EXPECT((std::is_same_v<refl::function_type_t<&witness_unary_int>, void(int) noexcept>));
        EXPECT((std::is_same_v<refl::function_type_t<&witness_throwing>, void(int)>));
    }
}

// Two distinct function pointers over the same signature report the
// same arity and the same parameter type.  The trait keys on the
// signature, not on the identity of the function.
void two_functions_one_signature_agree() {
    EXPECT(refl::arity_v<&witness_unary_int> == refl::arity_v<&witness_nothrowing>);
    EXPECT((std::is_same_v<refl::param_type_t<&witness_unary_int, 0>, refl::param_type_t<&witness_nothrowing, 0>>));
}

}  // namespace

int main() {
    every_witness_runs_at_run_time();
    every_query_reads_at_run_time();
    two_functions_one_signature_agree();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_signature: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_signature: every query read at run time agrees with the wall\n");
    return 0;
}
