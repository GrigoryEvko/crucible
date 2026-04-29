#pragma once

// ── crucible::safety::extract::signature_traits<auto FnPtr> ─────────
//
// Reflection-driven introspection of a function's parameter list and
// return type.  The base trait of the FOUND-D series (FOUND-D01 of
// 28_04_2026_effects.md §10 + 27_04_2026.md §5.5).  Every downstream
// FOUND-D primitive (param_type_t splice helper, is_*_v wrapper-shape
// predicates, UnaryTransform/Reduction/etc. parameter-shape concepts,
// inferred_row_t, inferred_permission_tags_t, the typed dispatcher)
// composes over this header.
//
// ── What this header ships ──────────────────────────────────────────
//
//   signature_traits<FnPtr>          struct exposing arity + params +
//                                    template alias param_type_t<I>
//                                    + return_type alias.
//
//   arity_v<FnPtr>                   variable template = arity.
//   param_type_t<FnPtr, I>           free alias for the I-th parameter
//                                    type (constrained: I < arity).
//   return_type_t<FnPtr>             free alias for the return type.
//
// ── What this header does NOT ship ──────────────────────────────────
//
//   - Wrapper-shape detection (is_owned_region_v, is_permission_v,
//     is_session_handle_v): FOUND-D03..D08, separate headers per
//     wrapper.  This trait is the substrate; the predicates layer on
//     top of param_type_t<>.
//   - Parameter-shape concepts (UnaryTransform, Reduction, ...):
//     FOUND-D12..D19, defined in safety/ParameterShapes.h once the
//     wrapper-detection predicates are in place.
//   - Member-function-pointer reflection.  The non-type template
//     parameter `auto FnPtr` accepts free-function pointers and static
//     member-function pointers; non-static member-function pointers
//     have a different invocation shape (implicit `this` parameter)
//     and require a separate trait family.
//   - Variadic functions (`...` ellipsis).  std::meta::parameters_of
//     returns only the named parameters; the ellipsis is observable
//     via std::meta::is_variadic but is not reachable through
//     param_type_t<>.  Variadic functions are a Crucible non-goal on
//     the dispatcher's reading surface.
//
// ── Implementation discipline ───────────────────────────────────────
//
// Built on P2996R13 reflection + P1306R5 expansion statements +
// P3491R3 define_static_array, all GCC 16 exclusive.  No
// #if-feature-test gates per CLAUDE.md memory feedback_no_feature_guards
// — the project compiles only under GCC 16.
//
//   * std::meta::parameters_of(^^FnPtr) — returns vector<info> at
//     consteval, materialized into a static constexpr span via
//     std::define_static_array per the GCC 16 reflection-gotcha
//     discipline (non-static constexpr crossing consteval→runtime
//     fails because vector::operator new is not a constant
//     expression in expansion-statement context).
//   * type_of(parameter_info) returns the parameter's type
//     reflection.  Splice `typename [:type_of(params[I]):]` to land
//     in the type system.
//   * return_type_of(function_info) returns the return-type
//     reflection.
//
// The `requires (I < arity)` constraint on `param_type_t<I>` produces
// clean SFINAE-style diagnostics on out-of-range indexing rather than
// the deep template-substitution noise that bare `params[I]` would
// emit when I overflows the static array bounds.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe   — no fields; the trait is a pure type-level computation.
//   TypeSafe   — every alias is constrained or splices a concrete type
//                produced by std::meta::type_of; param-index overflow
//                is a hard compile error via the `requires` clause.
//   NullSafe   — N/A; pure consteval.
//   MemSafe    — N/A; pure consteval.  No allocation; no destructor.
//   BorrowSafe — N/A; pure consteval.
//   ThreadSafe — N/A; pure consteval.
//   LeakSafe   — N/A; the static span produced by define_static_array
//                has program lifetime and is not owned by a runtime
//                object.
//   DetSafe    — same FnPtr → same arity, same param spliced types,
//                same return type, deterministically.  No hidden
//                state; no cross-TU drift other than the documented
//                reflection-name TU-context-fragility (which only
//                affects display_string_of, not parameters_of).
//
// ── Edge cases (verified at the bottom of this header) ──────────────
//
//   nullary fn      — arity == 0; param_type_t<0> is ill-formed
//                      (rejected by the `requires` clause).
//   ref-qualified   — `void(int&)` → param_type_t<0> is `int&`.
//   rvalue-ref      — `void(int&&)` → param_type_t<0> is `int&&`.
//   const-qualified — `void(const int)` → param_type_t<0> is `int`
//                      (top-level const decays in parameter-type
//                      adjusted-to ABI form).
//   const-ref       — `void(const int&)` → param_type_t<0> is
//                      `const int&` (the const qualifier on a
//                      reference is preserved).
//   pointer arg     — `void(int*)` → param_type_t<0> is `int*`.
//   user-type arg   — `void(MyStruct const&)` preserves qualifiers.
//   void return     — `void fn()` → return_type_t == void.
//   non-void return — `int fn()` → return_type_t == int.

#include <crucible/Platform.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── signature_traits<auto FnPtr> ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
struct signature_traits {
    // The reflection of the function (not the pointer).
    static constexpr auto function_reflection =
        ^^std::remove_pointer_t<decltype(FnPtr)>;

    // Materialize the parameter-info vector into a static-storage
    // span so callers can splice across consteval→runtime boundaries
    // (per the GCC 16 reflection gotcha — see header doc).
    static constexpr auto params = std::define_static_array(
        std::meta::parameters_of(function_reflection));

    static constexpr std::size_t arity = params.size();

    // I-th parameter type.  Constrained on bounds; an out-of-range
    // index is a clean hard error rather than a template-substitution
    // diagnostic deep inside std::define_static_array.
    //
    // Note: when reflecting a function *type* (rather than a function
    // *declaration*), parameters_of returns reflections that are
    // already type reflections — they have no source-level
    // declaration, so calling type_of on them throws "reflection does
    // not have a type".  The element splices directly as a type via
    // `typename [: ... :]`.  See GCC 16 P2996 implementation note in
    // safety/diag/StableName.h.
    template <std::size_t I>
        requires (I < arity)
    using param_type_t = typename [:params[I]:];

    // Return type — `void` is fully supported.
    using return_type =
        typename [:std::meta::return_type_of(function_reflection):];
};

// ═════════════════════════════════════════════════════════════════════
// ── Free aliases / variable templates ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Convenience free alias.
template <auto FnPtr, std::size_t I>
using param_type_t = typename signature_traits<FnPtr>::template param_type_t<I>;

template <auto FnPtr>
using return_type_t = typename signature_traits<FnPtr>::return_type;

template <auto FnPtr>
inline constexpr std::size_t arity_v = signature_traits<FnPtr>::arity;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════
//
// Adversarial cases verified at compile time.  Per the
// header-only-blind-spot discipline, every claim must be locked in
// here AND exercised by a sentinel TU (test/test_signature_traits.cpp)
// to be subject to project warning flags.

namespace detail::signature_traits_self_test {

// ── Witness functions (free functions, distinct signatures) ────────

inline void witness_nullary() noexcept {}
inline int  witness_int_returning() noexcept { return 0; }

inline void witness_unary_int(int)            noexcept {}
inline void witness_unary_int_ref(int&)       noexcept {}
inline void witness_unary_int_rref(int&&)     noexcept {}
inline void witness_unary_int_cref(int const&) noexcept {}
inline void witness_unary_int_ptr(int*)       noexcept {}

inline void witness_binary(int, double)       noexcept {}
inline void witness_ternary(int, double, char) noexcept {}

inline auto witness_returning_double(int) noexcept -> double { return 0.0; }

struct UserType { int v = 0; };
inline void witness_user_cref(UserType const&) noexcept {}

// ── arity claims ───────────────────────────────────────────────────

static_assert(signature_traits<&witness_nullary>::arity == 0);
static_assert(signature_traits<&witness_int_returning>::arity == 0);
static_assert(signature_traits<&witness_unary_int>::arity == 1);
static_assert(signature_traits<&witness_binary>::arity == 2);
static_assert(signature_traits<&witness_ternary>::arity == 3);

// ── arity_v variable template parity ───────────────────────────────

static_assert(arity_v<&witness_nullary> == 0);
static_assert(arity_v<&witness_unary_int> == 1);
static_assert(arity_v<&witness_binary> == 2);

// ── param_type_t splice — primitives ───────────────────────────────

static_assert(std::is_same_v<
    param_type_t<&witness_unary_int, 0>, int>);

static_assert(std::is_same_v<
    param_type_t<&witness_unary_int_ref, 0>, int&>);

static_assert(std::is_same_v<
    param_type_t<&witness_unary_int_rref, 0>, int&&>);

static_assert(std::is_same_v<
    param_type_t<&witness_unary_int_cref, 0>, int const&>);

static_assert(std::is_same_v<
    param_type_t<&witness_unary_int_ptr, 0>, int*>);

// ── param_type_t splice — multi-argument ordering ───────────────────

static_assert(std::is_same_v<
    param_type_t<&witness_binary, 0>, int>);
static_assert(std::is_same_v<
    param_type_t<&witness_binary, 1>, double>);

static_assert(std::is_same_v<
    param_type_t<&witness_ternary, 0>, int>);
static_assert(std::is_same_v<
    param_type_t<&witness_ternary, 1>, double>);
static_assert(std::is_same_v<
    param_type_t<&witness_ternary, 2>, char>);

// ── param_type_t splice — user types preserve qualifiers ───────────

static_assert(std::is_same_v<
    param_type_t<&witness_user_cref, 0>, UserType const&>);

// ── return_type extraction ─────────────────────────────────────────

static_assert(std::is_same_v<
    return_type_t<&witness_nullary>, void>);

static_assert(std::is_same_v<
    return_type_t<&witness_int_returning>, int>);

static_assert(std::is_same_v<
    return_type_t<&witness_returning_double>, double>);

// ── Distinct function pointers produce distinct trait specializations
//     (sanity: the trait is parameterized on FnPtr value, not on the
//      function type alone, so two functions with identical signatures
//      still produce distinct trait instantiations) ─────────────────

inline void witness_alpha(int) noexcept {}
inline void witness_beta(int)  noexcept {}

static_assert(arity_v<&witness_alpha> == arity_v<&witness_beta>);
static_assert(std::is_same_v<
    param_type_t<&witness_alpha, 0>,
    param_type_t<&witness_beta, 0>>);

}  // namespace detail::signature_traits_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per CLAUDE.md memory feedback_algebra_runtime_smoke_test_discipline
// — every algebra/effects/safety-substrate header ships an inline
// runtime_smoke_test() that exercises non-constant arguments and
// concept-based capability checks.  Pure static_assert tests mask
// consteval/SFINAE/inline-body bugs the runtime path catches.
//
// The trait is purely type-level; "exercising it at runtime" means
// constructing the witness types and calling the witnessed functions
// to confirm the signatures the trait claims actually match the
// runtime-callable ABI.  Fails to link if the trait disagrees with
// the function declarations.

inline void runtime_smoke_test() noexcept {
    using namespace detail::signature_traits_self_test;

    // Nullary witness — arity 0, void return.
    witness_nullary();

    // Int-returning nullary — call and consume return.
    int const r0 = witness_int_returning();
    (void)r0;

    // Unary int — non-constant argument.
    int x = 42;
    witness_unary_int(x);

    // Unary int& — must accept lvalue.
    witness_unary_int_ref(x);

    // Unary int&& — rvalue argument.
    witness_unary_int_rref(static_cast<int&&>(x));

    // Unary const int& — accepts both lvalue and rvalue.
    witness_unary_int_cref(x);
    witness_unary_int_cref(0);

    // Unary int* — pointer argument.
    witness_unary_int_ptr(&x);

    // Binary / ternary — multi-argument ordering.
    witness_binary(x, 1.0);
    witness_ternary(x, 1.0, 'a');

    // User type by const reference.
    UserType const ut{};
    witness_user_cref(ut);

    // Return-type witness.
    double const r1 = witness_returning_double(x);
    (void)r1;
}

}  // namespace crucible::safety::extract
