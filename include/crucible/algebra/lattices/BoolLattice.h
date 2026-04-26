#pragma once

// ── crucible::algebra::lattices::BoolLattice<Pred> ──────────────────
//
// Per-predicate singleton lattice — the foundation for Refined<Pred, T>
// per 25_04_2026.md §2.3:
//
//     using Refined<Pred, T> = Graded<Absolute, BoolLattice<Pred>, T>;
//
// ── Why "singleton"? ────────────────────────────────────────────────
//
// A Refined<Pred, T> value is constructed only when Pred holds — the
// validation runs at construction; once constructed, the predicate
// holds for the lifetime of the value (Refined is move-only and the
// predicate-witness can't be invalidated post-hoc).  So at runtime
// every Refined<Pred, T> is at the SAME lattice position: "Pred
// holds".  There is no "Pred unknown" or "Pred fails" runtime state —
// such values would never have been constructed.
//
// Therefore BoolLattice<Pred> has a SINGLE element (encoded in an
// EMPTY tag struct at the type level), and all lattice operations
// are trivially identity.  The empty element_type collapses via
// `[[no_unique_address]]` in Graded, giving Refined<P, T> sizeof(T)
// — matching the existing safety::Refined<P, T> zero-overhead
// guarantee that MIGRATE-2 (#462) preserves.
//
// ── Cross-predicate subsumption ─────────────────────────────────────
//
// Refined<positive, int> can flow to a position expecting
// Refined<non_negative, int> because `positive` logically implies
// `non_negative`.  This subsumption is NOT inside BoolLattice<P>'s
// definition (P and Q are different lattices) — it lives in
// SessionPayloadSubsort.h's `is_subsort` axioms gated by `implies_v
// <P, Q>` (shipped per #227).  BoolLattice's job is the per-predicate
// singleton structure; cross-predicate subsumption is the job of
// the subsort family.
//
//   Axiom coverage: TypeSafe — Pred is encoded at the type level, no
//                   runtime tag.  The Refined invariant (predicate
//                   holds) is established at construction and trusted
//                   downstream as `[[assume]]` for the optimizer.
//   Runtime cost:   zero — empty element_type collapses via EBO.
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h);
// SessionPayloadSubsort.h for cross-predicate subsumption axioms;
// MIGRATE-2 (#462) for the Refined<P, T> alias instantiation.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── BoolLattice<Pred>: per-predicate singleton sub-lattice ──────────
template <typename Pred>
struct BoolLattice {
    // Empty tag.  Pred is captured at the type level; the element
    // carries no runtime state.  Comparison is trivially true (only
    // one possible value — "Pred holds").
    struct element_type {
        using predicate_type = Pred;
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
            return true;
        }
    };

    using predicate_type = Pred;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
    [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    // Diagnostic name — reflection-derived from Pred's display string.
    // Used by SessionDiagnostic / Cipher serialize / debug print to
    // identify which predicate the BoolLattice carries.  E.g.
    // BoolLattice<positive>::name() returns "positive".
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return std::meta::display_string_of(^^Pred);
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::bool_lattice_self_test {

// Three witness predicates — each is a stateless tag struct with a
// `check<T>(v)` static method (the convention of safety::Refined's
// predicate library).  Only the type matters here for BoolLattice;
// `check` is consumed by Refined<P, T> at construction, not by
// BoolLattice.
struct positive {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept { return v > T{0}; }
};
struct non_negative {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept { return v >= T{0}; }
};
struct non_zero {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept { return v != T{0}; }
};

// Concept conformance.
static_assert(Lattice<BoolLattice<positive>>);
static_assert(BoundedLattice<BoolLattice<positive>>);
static_assert(Lattice<BoolLattice<non_negative>>);
static_assert(Lattice<BoolLattice<non_zero>>);

// Empty element_type for EBO collapse — load-bearing for Refined<>'s
// zero-overhead guarantee.
static_assert(std::is_empty_v<BoolLattice<positive>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_negative>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_zero>::element_type>);

// Lattice axioms hold (trivially — single-element lattice).  Use
// arbitrary witnesses; they're all the same value.
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<positive>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_negative>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_zero>>(
    {}, {}, {}));

// Diagnostic name comes from reflection on Pred.  GCC 16's
// std::meta::display_string_of returns a name whose qualification
// depth depends on the including TU's scope context — sometimes the
// simple name "positive", sometimes the fully-qualified
// "crucible::algebra::lattices::detail::bool_lattice_self_test::positive"
// (probed empirically on safety/Refined.h's TU).  Per the
// gcc16_c26_reflection_gotchas memory rule #5: verify with
// .ends_with() rather than equality so the assertion is robust
// across TU contexts.
static_assert(BoolLattice<positive>::name().ends_with("positive"));
static_assert(BoolLattice<non_negative>::name().ends_with("non_negative"));
static_assert(BoolLattice<non_zero>::name().ends_with("non_zero"));

// predicate_type alias is correct.
static_assert(std::is_same_v<BoolLattice<positive>::predicate_type, positive>);
static_assert(std::is_same_v<BoolLattice<positive>::element_type::predicate_type, positive>);

// ── Layout invariants on Graded<...,BoolLattice<P>,T> ───────────────
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using RefinedPositive = Graded<ModalityKind::Absolute, BoolLattice<positive>, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, EightByteValue);
// Arithmetic T witnesses — pin the macro's correctness across the
// trivially-default-constructible-T axis that the AUDIT-FOUNDATION
// pass dropped from the macro's parity checks.  Without these, a
// future tighter macro that re-introduced tdc parity would silently
// pass per-lattice headers but break MIGRATE-2's Refined<positive,
// int> production usage.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, double);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra/lattices/ header must exercise lattice ops AND
// Graded<...,L,T>::weaken/compose with non-constant arguments to
// catch consteval/SFINAE traps that pure static_assert tests miss.
inline void runtime_smoke_test() {
    using L = BoolLattice<positive>;
    L::element_type a{};
    L::element_type b{};
    [[maybe_unused]] bool             l = L::leq(a, b);
    [[maybe_unused]] L::element_type  j = L::join(a, b);
    [[maybe_unused]] L::element_type  m = L::meet(a, b);

    OneByteValue v{42};
    RefinedPositive<OneByteValue> initial{v, L::bottom()};
    auto widened   = initial.weaken(L::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(L::top());
    auto rv_comp   = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
}

}  // namespace detail::bool_lattice_self_test

}  // namespace crucible::algebra::lattices
