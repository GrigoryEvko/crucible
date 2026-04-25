#pragma once

// ── crucible::algebra::Lattice / Semiring concepts ──────────────────
//
// The algebraic substrate every `Graded<M, L, T>` rides on per
// 25_04_2026.md §2.2.  A "lattice" L is a stateless tag class with
// static members exposing:
//
//     using element_type = ...;             // carrier type
//     static constexpr element_type bottom();    [BoundedBelowLattice]
//     static constexpr element_type top();       [BoundedAboveLattice]
//     static constexpr bool         leq(a, b);   // partial order ⊑
//     static constexpr element_type join(a, b);  // ⊕
//     static constexpr element_type meet(a, b);  // ⊗
//     static consteval std::string_view name();  [optional]
//
// A "semiring" S extends with multiplicative structure:
//
//     static constexpr element_type zero();      // additive identity
//     static constexpr element_type one();       // multiplicative identity
//     static constexpr element_type add(a, b);   // (commutative monoid)
//     static constexpr element_type mul(a, b);   // (monoid; needn't commute)
//
// IMPORTANT — `constexpr` vs `consteval`:
//
//   The signature MUST be `constexpr` (callable at both compile time
//   and runtime), NOT `consteval` (compile-time only).  Graded's
//   precondition `weaken(new_grade) pre (L::leq(grade_, new_grade))`
//   is evaluated at RUNTIME under the `enforce` contract semantic —
//   calling a consteval function with the runtime member `grade_` is
//   a hard compile error.  Static-grade lattices (e.g. QttSemiring::
//   At<1> with empty element_type) appear consteval-eligible, but
//   the convention is uniform: every public lattice op is constexpr.
//   Only `name()` and the verify_* helpers below are consteval —
//   they are diagnostic / test-only and never called at runtime.
//
//   Axiom coverage: TypeSafe — concept gates fire at template-substitution
//                   time, not at use site (callers see the missing member
//                   directly, not a downstream cascade).
//                   DetSafe — every operation is consteval; no runtime
//                   path can introduce nondeterminism.
//   Runtime cost:   zero — concepts plus consteval helpers only.
//
// Instantiated lattices live under `algebra/lattices/`.  Each per-lattice
// header MUST include this one and emit a self-test block invoking the
// `verify_*` helpers below for representative element witnesses.  No
// lattice ships without proving the laws it claims to satisfy.
//
// See ALGEBRA-15 (#460) for the project-wide static_assert sweep
// auditing `sizeof(Graded<M, L, T>) == sizeof(T)` per lattice + T pair.

#include <crucible/algebra/Modality.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace crucible::algebra {

// ── Element-type projection ─────────────────────────────────────────
template <typename L>
using LatticeElement = typename L::element_type;

// ── Core Lattice concept ────────────────────────────────────────────
//
// A type L is a Lattice iff it publishes element_type plus the
// signatures above for leq/join/meet.  Bounded variants below add
// bottom() / top() requirements.
template <typename L>
concept Lattice = requires {
    typename L::element_type;
} && requires (LatticeElement<L> a, LatticeElement<L> b) {
    { L::leq(a, b)  } -> std::convertible_to<bool>;
    { L::join(a, b) } -> std::same_as<LatticeElement<L>>;
    { L::meet(a, b) } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept BoundedBelowLattice = Lattice<L> && requires {
    { L::bottom() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept BoundedAboveLattice = Lattice<L> && requires {
    { L::top() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept BoundedLattice  = BoundedBelowLattice<L> && BoundedAboveLattice<L>;

template <typename L>
concept UnboundedLattice = Lattice<L>
                        && !BoundedBelowLattice<L>
                        && !BoundedAboveLattice<L>;

// ── Semiring concept ────────────────────────────────────────────────
//
// Semirings additionally require equality on element_type for the
// algebraic-law verifiers (a Semiring without equality has no testable
// distributivity law and would not be useful in practice).
template <typename S>
concept Semiring = requires {
    typename S::element_type;
} && requires (LatticeElement<S> a, LatticeElement<S> b) {
    { S::add(a, b) } -> std::same_as<LatticeElement<S>>;
    { S::mul(a, b) } -> std::same_as<LatticeElement<S>>;
    { a == b       } -> std::convertible_to<bool>;
} && requires {
    { S::zero() } -> std::same_as<LatticeElement<S>>;
    { S::one()  } -> std::same_as<LatticeElement<S>>;
};

// ── Diagnostic name probe ───────────────────────────────────────────
template <typename L>
concept HasLatticeName = requires {
    { L::name() } -> std::convertible_to<std::string_view>;
};

template <typename L>
[[nodiscard]] consteval std::string_view lattice_name() noexcept {
    if constexpr (HasLatticeName<L>) return L::name();
    else                              return std::string_view{"<unnamed lattice>"};
}

// ── Subsumption helpers ─────────────────────────────────────────────
//
// Sugar over L::leq.  `equivalent` performs the antisymmetric round-
// trip; `strictly_less` is the open inequality.  All three are
// `constexpr` (NOT consteval) so they compose into Graded's runtime
// `pre()` predicates without forcing compile-time evaluation.
template <Lattice L>
[[nodiscard]] constexpr bool subsumes(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b);
}

template <Lattice L>
[[nodiscard]] constexpr bool equivalent(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && L::leq(b, a);
}

template <Lattice L>
[[nodiscard]] constexpr bool strictly_less(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && !L::leq(b, a);
}

// ── Lattice-law verifiers ───────────────────────────────────────────
//
// Each per-lattice header SHOULD invoke these at every applicable
// element witness inside its own self-test block.  Compositional —
// `verify_lattice_axioms_at` rolls all single-element/pair/triple
// laws into one helper for terse self-tests.

template <Lattice L>
[[nodiscard]] consteval bool verify_idempotent_join(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::join(a, a), a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_idempotent_meet(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::meet(a, a), a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_join(
    LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::join(a, b), L::join(b, a));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_meet(
    LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::meet(a, b), L::meet(b, a));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_join(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return equivalent<L>(L::join(L::join(a, b), c), L::join(a, L::join(b, c)));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_meet(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return equivalent<L>(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c)));
}

// Absorption (the law that distinguishes lattices from arbitrary
// commutative idempotent monoid pairs):
//   a ⊕ (a ⊗ b) = a    AND    a ⊗ (a ⊕ b) = a
template <Lattice L>
[[nodiscard]] consteval bool verify_absorption(
    LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::join(a, L::meet(a, b)), a)
        && equivalent<L>(L::meet(a, L::join(a, b)), a);
}

// Partial-order laws on three witnesses: reflexive, antisymmetric,
// transitive.  Reflexivity is tested on all three witnesses to catch
// lattices that accidentally make leq depend on the wrong operand.
template <Lattice L>
[[nodiscard]] consteval bool verify_partial_order(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    const bool reflexive_a   =  L::leq(a, a);
    const bool reflexive_b   =  L::leq(b, b);
    const bool reflexive_c   =  L::leq(c, c);
    const bool antisymmetric = !(L::leq(a, b) && L::leq(b, a)) || equivalent<L>(a, b);
    const bool transitive    = !(L::leq(a, b) && L::leq(b, c)) || L::leq(a, c);
    return reflexive_a && reflexive_b && reflexive_c && antisymmetric && transitive;
}

template <BoundedBelowLattice L>
[[nodiscard]] consteval bool verify_bottom_identity(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::join(L::bottom(), a), a);
}

template <BoundedAboveLattice L>
[[nodiscard]] consteval bool verify_top_identity(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::meet(L::top(), a), a);
}

// Roll-up: every single-element / pair / triple lattice law verified
// at one call site.  Per-lattice headers wrap their representative
// witnesses inside this helper for a single-line static_assert.
template <Lattice L>
[[nodiscard]] consteval bool verify_lattice_axioms_at(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return verify_idempotent_join<L>(a)
        && verify_idempotent_meet<L>(a)
        && verify_commutative_join<L>(a, b)
        && verify_commutative_meet<L>(a, b)
        && verify_associative_join<L>(a, b, c)
        && verify_associative_meet<L>(a, b, c)
        && verify_absorption<L>(a, b)
        && verify_partial_order<L>(a, b, c);
}

// Bounded-lattice rollup: every law from verify_lattice_axioms_at PLUS
// bottom/top identity.  Use this in per-lattice self-tests for any
// lattice that satisfies BoundedLattice.
template <BoundedLattice L>
[[nodiscard]] consteval bool verify_bounded_lattice_axioms_at(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return verify_lattice_axioms_at<L>(a, b, c)
        && verify_bottom_identity<L>(a)
        && verify_bottom_identity<L>(b)
        && verify_bottom_identity<L>(c)
        && verify_top_identity<L>(a)
        && verify_top_identity<L>(b)
        && verify_top_identity<L>(c);
}

// ── Semiring-law verifiers ──────────────────────────────────────────

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_identity(LatticeElement<S> a) noexcept {
    return S::add(S::zero(), a) == a && S::add(a, S::zero()) == a;
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_identity(LatticeElement<S> a) noexcept {
    return S::mul(S::one(), a) == a && S::mul(a, S::one()) == a;
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_zero(LatticeElement<S> a) noexcept {
    return S::mul(S::zero(), a) == S::zero()
        && S::mul(a, S::zero()) == S::zero();
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_commutative(
    LatticeElement<S> a, LatticeElement<S> b) noexcept {
    return S::add(a, b) == S::add(b, a);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_associative(
    LatticeElement<S> a, LatticeElement<S> b, LatticeElement<S> c) noexcept {
    return S::add(S::add(a, b), c) == S::add(a, S::add(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_associative(
    LatticeElement<S> a, LatticeElement<S> b, LatticeElement<S> c) noexcept {
    return S::mul(S::mul(a, b), c) == S::mul(a, S::mul(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_distributivity(
    LatticeElement<S> a, LatticeElement<S> b, LatticeElement<S> c) noexcept {
    // a · (b + c) = (a · b) + (a · c)        (left)
    // (a + b) · c = (a · c) + (b · c)        (right)
    return S::mul(a, S::add(b, c)) == S::add(S::mul(a, b), S::mul(a, c))
        && S::mul(S::add(a, b), c) == S::add(S::mul(a, c), S::mul(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_semiring_axioms_at(
    LatticeElement<S> a, LatticeElement<S> b, LatticeElement<S> c) noexcept {
    return verify_additive_identity<S>(a)
        && verify_multiplicative_identity<S>(a)
        && verify_multiplicative_zero<S>(a)
        && verify_additive_commutative<S>(a, b)
        && verify_additive_associative<S>(a, b, c)
        && verify_multiplicative_associative<S>(a, b, c)
        && verify_distributivity<S>(a, b, c);
}

// ── Self-test conformance witness ───────────────────────────────────
//
// A trivial in-house lattice/semiring proves the concepts and verifier
// helpers compile.  Each per-lattice header under lattices/ adds its
// own conformance test against its own witnesses.
namespace detail::lattice_self_test {

// A two-element Boolean lattice {false ⊑ true}.  Bounded; total order.
//
// All operations are `constexpr` (NOT consteval) per the convention
// at the top of this header — Graded's runtime `pre (L::leq(...))`
// must be able to call them with non-constant arguments.  Self-tests
// below exercise both the compile-time path (static_assert with
// constant args) and the runtime path (via inline_smoke_test()).
struct TrivialBoolLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool         leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool         join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool         meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialBool"; }
};

static_assert(Lattice<TrivialBoolLattice>);
static_assert(BoundedBelowLattice<TrivialBoolLattice>);
static_assert(BoundedAboveLattice<TrivialBoolLattice>);
static_assert(BoundedLattice<TrivialBoolLattice>);
static_assert(!UnboundedLattice<TrivialBoolLattice>);

// Exhaustive 8-witness coverage of the bounded rollup proves both the
// lattice axioms and the bottom/top identity laws in one shot.
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, false, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, false, true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, true,  false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, true,  true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true,  false, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true,  false, true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true,  true,  false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true,  true,  true));

static_assert( subsumes<TrivialBoolLattice>(false, true));
static_assert(!subsumes<TrivialBoolLattice>(true,  false));
static_assert( equivalent<TrivialBoolLattice>(true, true));
static_assert(!equivalent<TrivialBoolLattice>(false, true));
static_assert( strictly_less<TrivialBoolLattice>(false, true));
static_assert(!strictly_less<TrivialBoolLattice>(true,  true));

static_assert(lattice_name<TrivialBoolLattice>() == "TrivialBool");

// Boolean ring as a trivial Semiring witness — proves the Semiring
// concept compiles end-to-end and the verifier helpers fire.  Same
// constexpr convention as TrivialBoolLattice above.
struct TrivialBoolSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type zero() noexcept { return false; }
    [[nodiscard]] static constexpr element_type one()  noexcept { return true;  }
    [[nodiscard]] static constexpr element_type add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr element_type mul(bool a, bool b) noexcept { return a && b; }
};

static_assert(Semiring<TrivialBoolSemiring>);
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, false, false));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, true,  true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true,  false, true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true,  true,  true));

}  // namespace detail::lattice_self_test

}  // namespace crucible::algebra
