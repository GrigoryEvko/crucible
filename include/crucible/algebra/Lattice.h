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
// ── Centralized law witness (fix-09) ────────────────────────────────
//
// The `Lattice<L>` concept is NOT signature-only: beyond leq/join/meet
// existing, it requires that L's OWN CANONICAL WITNESSES (its bottom()/
// top(), or a default-constructed element_type for the unbounded case)
// satisfy the lattice axioms.  This makes law-conformance STRUCTURAL —
// a lattice with correct signatures but a non-idempotent / non-
// associative / non-commutative / non-absorbing join FAILS `Lattice<L>`
// and so cannot be used as a `Graded<M, L, T>` grade.  The per-lattice
// self-test blocks (exhaustive over finite carriers, spot-check over
// ℕ / ℚ) remain the FULL coverage; the concept gate is the mechanical
// FLOOR no contributor can forget.  See the `detail::lattice_laws`
// machinery and the `Lattice` concept below.
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

// ── Structural-signature probe (NOT the public Lattice concept) ─────
//
// `LatticeShape<L>` is the SIGNATURE-only check: L publishes
// element_type plus leq/join/meet with the right shapes.  It is the
// pre-witness gate the law-witness machinery below builds on — every
// law verifier needs the signatures to exist before it can evaluate
// them.  Downstream code MUST use the strengthened `Lattice<L>`
// concept (below), which additionally demands a tractable law
// WITNESS; `LatticeShape` is internal scaffolding so the witness
// predicate can name the members without recursing through `Lattice`.
template <typename L>
concept LatticeShape = requires {
    typename L::element_type;
} && requires (LatticeElement<L> a, LatticeElement<L> b) {
    { L::leq(a, b)  } -> std::convertible_to<bool>;
    { L::join(a, b) } -> std::same_as<LatticeElement<L>>;
    { L::meet(a, b) } -> std::same_as<LatticeElement<L>>;
};

// ── Bounded-signature probes (NOT yet law-witnessed) ────────────────
//
// Signature-only bottom()/top() probes, expressed over LatticeShape so
// the canonical-witness selector below can ask "does L expose bottom()
// / top()?" WITHOUT triggering the law-witness requirement (which is
// the very thing those probes feed).  The PUBLIC BoundedBelowLattice /
// BoundedAboveLattice concepts (further down) are stated over the
// strengthened `Lattice` and so carry the law witness transitively.
template <typename L>
concept HasBottom = LatticeShape<L> && requires {
    { L::bottom() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept HasTop = LatticeShape<L> && requires {
    { L::top() } -> std::same_as<LatticeElement<L>>;
};

// ── Centralized law-witness machinery ───────────────────────────────
//
// The structural signatures above only prove leq/join/meet EXIST — a
// contributor can ship a lattice with a non-idempotent or non-
// associative join and the SIGNATURE check happily accepts it.  The
// per-lattice self-test blocks under lattices/ DO call the verify_*
// helpers, but that is a CONVENTION a new lattice can silently omit;
// the omission would feed a non-lattice into Graded::compose and into
// row_hash federation keying with no diagnostic.
//
// To make law-conformance STRUCTURAL (a witness-less or law-violating
// lattice fails `Lattice<L>` and cannot be used as a Graded grade), the
// concept below requires that the lattice's OWN CANONICAL WITNESSES
// satisfy the lattice axioms.  No per-lattice header edit is needed:
// the witnesses are derived centrally from each lattice's bottom()/
// top() (every concrete lattice in the tree is bounded) — or, for the
// concept-permitted unbounded case, from a default-constructed
// element_type.
//
// This catches the COMMON law break (a join that is non-idempotent /
// non-commutative / non-associative at bottom/top — i.e. everywhere on
// a 2+-element lattice) at the concept gate.  Interior-only breaks on
// infinite carriers remain the domain of the exhaustive per-lattice
// self-tests (Lattice.h cannot exhaust ℚ or ℕ); the concept gate is the
// mechanical FLOOR no contributor can forget, not a replacement for the
// per-lattice spot/exhaustive checks.
//
// IMPORTANT — no recursion: these helpers are templated on plain
// `typename L` constrained by `LatticeShape` (the signature probe), NOT
// by the `Lattice` concept they help define.  A `Lattice`-constrained
// helper here would make the concept self-referential.
namespace detail::lattice_laws {

// Raw (unconstrained-by-`Lattice`) equivalence + axiom checks.  These
// mirror the public verify_* helpers below but stay on the LatticeShape
// signature probe so the concept can call them without recursion.
template <LatticeShape L>
[[nodiscard]] consteval bool raw_equivalent(
    LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && L::leq(b, a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool raw_axioms_at(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    // Idempotence (join, meet).
    const bool idem_join = raw_equivalent<L>(L::join(a, a), a);
    const bool idem_meet = raw_equivalent<L>(L::meet(a, a), a);
    // Commutativity (join, meet).
    const bool comm_join = raw_equivalent<L>(L::join(a, b), L::join(b, a));
    const bool comm_meet = raw_equivalent<L>(L::meet(a, b), L::meet(b, a));
    // Associativity (join, meet).
    const bool assoc_join =
        raw_equivalent<L>(L::join(L::join(a, b), c), L::join(a, L::join(b, c)));
    const bool assoc_meet =
        raw_equivalent<L>(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c)));
    // Absorption (the lattice-defining law).
    const bool absorb = raw_equivalent<L>(L::join(a, L::meet(a, b)), a)
                     && raw_equivalent<L>(L::meet(a, L::join(a, b)), a);
    // Partial-order: reflexive on each witness, antisymmetric,
    // transitive on the (a, b, c) chain.
    const bool reflexive    = L::leq(a, a) && L::leq(b, b) && L::leq(c, c);
    const bool antisymmetric =
        !(L::leq(a, b) && L::leq(b, a)) || raw_equivalent<L>(a, b);
    const bool transitive   = !(L::leq(a, b) && L::leq(b, c)) || L::leq(a, c);
    return idem_join && idem_meet && comm_join && comm_meet
        && assoc_join && assoc_meet && absorb
        && reflexive && antisymmetric && transitive;
}

// Canonical-witness law check.  Picks the lattice's own representative
// elements and evaluates raw_axioms_at over the cross-product:
//
//   • Bounded (bottom() AND top()) — the universal case in the tree:
//     evaluate over every triple drawn from {bottom, top}, exercising
//     mixed witnesses so commutativity / associativity / absorption are
//     stressed on a genuine two-point span (when bottom ≠ top) and the
//     degenerate one-point span (singleton lattices: bottom == top).
//   • One-bounded (only bottom() XOR top()) — single available extreme.
//   • Unbounded (neither) — default-constructed element_type witness;
//     catches at least idempotence + the single-element partial order.
//     No concrete lattice in the tree is unbounded, but the base
//     `Lattice` concept permits it, so the witness must still exist.
template <LatticeShape L>
[[nodiscard]] consteval bool laws_hold() noexcept {
    if constexpr (HasBottom<L> && HasTop<L>) {
        const LatticeElement<L> lo = L::bottom();
        const LatticeElement<L> hi = L::top();
        return raw_axioms_at<L>(lo, lo, lo)
            && raw_axioms_at<L>(hi, hi, hi)
            && raw_axioms_at<L>(lo, hi, lo)
            && raw_axioms_at<L>(hi, lo, hi)
            && raw_axioms_at<L>(lo, lo, hi)
            && raw_axioms_at<L>(hi, hi, lo)
            && raw_axioms_at<L>(lo, hi, hi)
            && raw_axioms_at<L>(hi, lo, lo);
    } else if constexpr (HasBottom<L>) {
        const LatticeElement<L> lo = L::bottom();
        return raw_axioms_at<L>(lo, lo, lo);
    } else if constexpr (HasTop<L>) {
        const LatticeElement<L> hi = L::top();
        return raw_axioms_at<L>(hi, hi, hi);
    } else if constexpr (std::is_default_constructible_v<LatticeElement<L>>) {
        const LatticeElement<L> e{};
        return raw_axioms_at<L>(e, e, e);
    } else {
        // No bottom()/top() AND element_type is not default-
        // constructible — no canonical witness exists, so L cannot
        // produce a tractable law witness and fails `Lattice<L>`.  No
        // concrete lattice in the tree reaches this branch.
        return false;
    }
}

}  // namespace detail::lattice_laws

// ── Core Lattice concept (signatures + centralized law witness) ─────
//
// A type L is a Lattice iff it (1) publishes element_type plus the
// leq/join/meet signatures AND (2) its canonical witnesses satisfy the
// lattice axioms.  Requirement (2) is the law WITNESS: a lattice with
// correct signatures but a non-idempotent / non-commutative / non-
// associative / non-absorbing join cannot satisfy the concept and so
// cannot be used as a `Graded<M, L, T>` grade.  Bounded variants below
// add bottom() / top() requirements over this strengthened base.
template <typename L>
concept Lattice = LatticeShape<L>
               && detail::lattice_laws::laws_hold<L>();

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

// ── Distributive-lattice axiom (Birkhoff) ───────────────────────────
//
// A LATTICE L is distributive iff:
//
//     a ∧ (b ∨ c) = (a ∧ b) ∨ (a ∧ c)
//     a ∨ (b ∧ c) = (a ∨ b) ∧ (a ∨ c)
//
// (The two laws are equivalent in any lattice — proving one implies
// the other — but verifying both at three witnesses gives stronger
// confidence the lattice author didn't accidentally implement a
// non-distributive variant.)
//
// Distinct from `verify_distributivity` (above), which is the
// multiplicative variant for Semirings: `a · (b + c) = (a·b) + (a·c)`.
// Lattice distributivity uses ∧/∨ (meet/join); semiring distributivity
// uses ·/+ (mul/add).  Same algebraic shape, different operations.
//
// Birkhoff's representation theorem: a finite lattice is distributive
// iff it embeds (as a sublattice) into some power set under
// (∩, ∪, ⊆).  Practical consequence: vector-clock lattices, prefix
// lattices, total-order lattices, and most "natural" lattices
// Crucible cares about are distributive.  The non-distributive
// counter-examples (M_3, N_5 — the "diamond" and "pentagon") arise
// in modal logic and free lattices but not in our wrappers.
//
// Use this verifier in any lattice header whose author CLAIMS the
// lattice is distributive.  General Lattice / BoundedLattice rollups
// do NOT include it — non-distributive lattices are valid lattices.
template <Lattice L>
[[nodiscard]] consteval bool verify_distributive_lattice(
    LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return equivalent<L>(L::meet(a, L::join(b, c)),
                         L::join(L::meet(a, b), L::meet(a, c)))
        && equivalent<L>(L::join(a, L::meet(b, c)),
                         L::meet(L::join(a, b), L::join(a, c)));
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

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// Drive every Lattice operation through a NON-constant argument path.
// The header doc-block (lines 24-34) asserts the constexpr-not-consteval
// discipline for every public lattice op — Graded's runtime
// pre(L::leq(...)) clause MUST be able to call them with non-constant
// member values.  Pure static_assert testing exercises only the
// consteval branch; this smoke test pins the runtime branch.
inline void runtime_smoke_test() {
    // Drive bottom/top/leq/join/meet through non-constant args.
    bool x = true;   // NOT constexpr — runtime value
    bool y = false;  // NOT constexpr — runtime value
    [[maybe_unused]] bool bot = TrivialBoolLattice::bottom();
    [[maybe_unused]] bool top = TrivialBoolLattice::top();
    [[maybe_unused]] bool le  = TrivialBoolLattice::leq(x, y);
    [[maybe_unused]] bool jo  = TrivialBoolLattice::join(x, y);
    [[maybe_unused]] bool me  = TrivialBoolLattice::meet(x, y);

    // Drive the lattice-relation helpers through non-constant args.
    [[maybe_unused]] bool sub = subsumes<TrivialBoolLattice>(y, x);
    [[maybe_unused]] bool eq  = equivalent<TrivialBoolLattice>(x, x);
    [[maybe_unused]] bool sl  = strictly_less<TrivialBoolLattice>(y, x);

    // Same for the semiring side.
    [[maybe_unused]] bool zer = TrivialBoolSemiring::zero();
    [[maybe_unused]] bool one = TrivialBoolSemiring::one();
    [[maybe_unused]] bool add = TrivialBoolSemiring::add(x, y);
    [[maybe_unused]] bool mul = TrivialBoolSemiring::mul(x, y);
}

}  // namespace detail::lattice_self_test

}  // namespace crucible::algebra
