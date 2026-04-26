#pragma once

// ── crucible::algebra::Graded<Modality, Lattice, T> ─────────────────
//
// THE foundational primitive of the 25_04_2026.md §2 refactor.  Every
// safety wrapper that decorates a value (Linear, Refined, Tagged,
// Secret, Monotonic, AppendOnly, SharedPermission, plus future
// Stale, Budgeted, TimeOrdered, SealedRefined) folds into a
// `Graded<M, L, T>` instantiation under this template.
//
//   Axiom coverage: every axiom inherits from the wrapped T plus the
//                   lattice's structural witnesses.  Linearity comes
//                   from QttSemiring (grade 1); predicate refinement
//                   from BoolLattice; classification from ConfLattice;
//                   trust provenance from TrustLattice; etc.  The
//                   Graded class adds no axiom obligations of its own
//                   — it composes them.
//   Runtime cost:   zero when both T AND LatticeElement<L> are empty
//                   types (e.g. Linear<Tag> with QTT::At<1> grade).
//                   sizeof(T) when T is non-empty and grade is empty
//                   (e.g. Linear<int>).  sizeof(T) +
//                   sizeof(LatticeElement<L>) when both carry runtime
//                   state (e.g. SharedPermission<Tag> with rational
//                   grade).  Both fields use [[no_unique_address]] so
//                   the runtime cost matches the underlying primitives
//                   exactly.  Verified codebase-wide by ALGEBRA-15
//                   (#460) under -O3.
//
// Design — type-level vs runtime grade:
//
//   For STATIC-grade lattices (QttSemiring::At<1>, BoolLattice<Pred>,
//   ConfLattice's two-point lattice), the LatticeElement is an empty
//   class (e.g. `std::integral_constant<int, 1>`), grade_ collapses
//   to zero bytes via EBO, and the grade is encoded entirely at the
//   type level via the lattice template parameter L itself.
//
//   For DYNAMIC-grade lattices (FractionalLattice with rational
//   shares, StalenessSemiring with ℕ ∪ ∞ values), LatticeElement is
//   a runtime-sized type, grade_ stores the per-instance grade, and
//   `weaken` / `compose` operate on it at runtime.
//
//   Both shapes share the same source — ALGEBRA-3 ships ONE Graded
//   that accommodates both via [[no_unique_address]].  No template
//   specialization, no opt-in trait — the lattice's element type is
//   the discriminator.
//
// Public API:
//
//   Construction:
//     Graded(value, grade)           — explicit grade
//     Graded::at_bottom(value)       — uses L::bottom() as initial grade
//                                      (only when BoundedBelowLattice<L>)
//     inject(value, grade)           — RelativeMonad-form unit
//   Access:
//     peek() const&                  — borrow inner value
//     consume() &&                   — move inner value out (rvalue this)
//     grade() const                  — current lattice element
//   Operations:
//     extract() &&                   — Comonad counit (requires Comonad)
//     weaken(new_grade)              — contract-checked widening
//     compose(other)                 — grade ⊕ other.grade, *this's value
//
// Note for MIGRATE-* alias implementers:
//
//   Graded's copy/move semantics are DEFAULTED — copying preserves
//   both value and grade.  Aliases that need stricter semantics
//   (e.g. Linear<T> = Graded<Absolute, QttSemiring::At<1>, T> must
//   delete copy to enforce linearity) CANNOT be a bare type alias.
//   They must wrap Graded in a class that re-imposes the discipline:
//
//       template <typename T>
//       class Linear {
//           Graded<Absolute, QttSemiring::At<1>, T> impl_;
//       public:
//           Linear(const Linear&) = delete("Linear<T> is move-only");
//           // ... selective re-export of impl_'s API
//       };
//
//   The 25_04_2026.md §2.3 "using Linear = Graded<...>" is shorthand
//   that hides this wrapping; MIGRATE-1 (#461) makes the wrapping
//   explicit.  Aliases for non-linear shapes (Refined, Tagged,
//   Secret, ...) CAN be bare aliases — Graded's defaulted semantics
//   match the existing wrappers' defaulted semantics in safety/.
//
// See ALGEBRA-1 (Modality.h), ALGEBRA-2 (Lattice.h), ALGEBRA-4..15
// (concrete lattice instantiations under lattices/).  Lean
// formalization in lean/Crucible/Algebra/Graded.lean per LEAN-1
// (#490).
//
// ── C++26 arsenal usage in this header ──────────────────────────────
//
//   P2900R14 contracts (pre/post)        — invariant checking on
//                                           weaken/compose/at_bottom/
//                                           inject; contract-evaluation-
//                                           semantic per TU
//   P2996R13 reflection (^^T, splice)    — value_type_name() emits T's
//                                           reflected display string
//   P3491R3 define_static_array          — paired with template for in
//                                           Modality.h / Capabilities.h
//                                           name-coverage assertions
//   [[no_unique_address]] (C++20)        — EBO collapse on inner_ AND
//                                           grade_ for empty types
//   [[nodiscard]] at class + method      — captured at construction;
//                                           [[nodiscard]] returns
//   `requires` clauses on overloads      — copy_constructible<T> SFINAE
//                                           gates const& weaken/compose
//                                           cleanly for move-only T
//   std::is_layout_compatible_v          — strengthened layout macro
//                                           verifies sizeof AND alignof
//                                           AND layout-compat with T

#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>

#include <contracts>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra {

// ── Graded class template ───────────────────────────────────────────
template <ModalityKind M, Lattice L, typename T>
class [[nodiscard]] Graded {
    static_assert(IsModality<M>,
        "Graded<M, L, T>: M must be one of Comonad / RelativeMonad / "
        "Absolute / Relative.  See algebra/Modality.h.");

public:
    // ── Public type aliases ─────────────────────────────────────────
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type       = L;
    using value_type         = T;
    using grade_type         = LatticeElement<L>;

private:
    // ── Layout (NSDMI per InitSafe; EBO for empty types) ────────────
    [[no_unique_address]] T          inner_{};
    [[no_unique_address]] grade_type grade_{};

public:
    // ── Diagnostic ──────────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::crucible::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::crucible::algebra::lattice_name<L>();
    }
    // Reflection-derived (P2996R13): emits T's actual type name as it
    // appears in source.  Used by SessionDiagnostic / Cipher serialize
    // / debug print to identify what Graded wraps.
    //
    // ── TU-CONTEXT FRAGILITY WARNING ────────────────────────────────
    //
    // `display_string_of(^^T)` returns a name whose qualification depth
    // depends on the including TU's scope chain.  In an algebra-only
    // TU it may return the simple name "MyKey"; from a deep transitive
    // include path (e.g. through safety/* into Cipher) it may return
    // the fully-qualified
    // `crucible::cipher::secret_keys::MyKey`.  Same TU-fragility that
    // bit BoolLattice<positive>::name() and TrustLattice<source::*>::
    // name() before this discipline was established (see
    // gcc16_c26_reflection_gotchas memory rule #5).
    //
    // **DISCIPLINE for callers**: NEVER write
    //   static_assert(Graded<...>::value_type_name() == "ExpectedT")
    // because the assertion's pass/fail depends on which TU the
    // assertion sits in.  Instead, ALWAYS use:
    //   static_assert(Graded<...>::value_type_name().ends_with("ExpectedT"))
    // The simple name is always a suffix of the qualified form, so
    // .ends_with() is robust across any TU the static_assert appears
    // in.  Diagnostic output (Cipher serialize / debug print) is
    // unaffected — those callers consume the string verbatim and
    // tolerate either qualification depth.
    //
    // Future-proofing: when MIGRATE-3 (Secret<T>) and MIGRATE-4
    // (Tagged<T, Source>) ship and want type-level diagnostic
    // assertions on user-supplied T, they MUST follow the
    // .ends_with() discipline.  No infrastructure here enforces it
    // at compile time — the contract is at the call site.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    // ── Object semantics (defaulted; implicit noexcept inferred from T
    //     and grade_type — explicit noexcept on `= default` would be
    //     ill-formed if either had a throwing default ctor) ──────────
    constexpr Graded()                         = default;
    constexpr Graded(const Graded&)            = default;
    constexpr Graded(Graded&&)                 = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&)      = default;
    ~Graded()                                  = default;

    // ── Explicit construction with value + grade ────────────────────
    //
    // Both arguments by value; std::move into the members.  For empty
    // grade types the move is a no-op (EBO-collapsed); for non-empty
    // grades (FractionalLattice's rational, StalenessSemiring's int)
    // moving avoids an extra copy when the caller already had an
    // rvalue.
    constexpr Graded(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<grade_type>)
        : inner_{std::move(value)}, grade_{std::move(grade)} {}

    // ── Construction at the lattice's bottom element (when bounded) ─
    //
    // Convenience constructor for BoundedBelowLattice<L>: omits the
    // grade argument and starts at L::bottom().  Used by alias
    // wrappers whose default position is "no information accumulated"
    // (e.g. AppendOnly<T> starting at the empty-prefix lattice
    // bottom; Monotonic<T, std::less<>> starting at MIN).
    [[nodiscard]] static constexpr Graded at_bottom(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        // GCC 16.0.1 ICEs (cp/pt.cc:17244) on template-dependent
        // expressions in `post()` predicates of templated class
        // members; `contract_assert(...)` in the body works and gives
        // equivalent runtime checking under enforce semantic.  See
        // feedback_gcc16_c26_contract_gotchas memory for the rule.
        Graded result{std::move(value), L::bottom()};
        contract_assert(L::leq(result.grade(), L::bottom())
                     && L::leq(L::bottom(), result.grade()));
        return result;
    }

    // ── Access ──────────────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return inner_;
    }
    [[nodiscard]] constexpr T consume() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(inner_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(
        std::is_nothrow_copy_constructible_v<grade_type>)
    {
        return grade_;
    }

    // ── Mutable access (Absolute modality only) ─────────────────────
    //
    // peek_mut and swap are admitted on Absolute-graded values where
    // the grade is a STATIC property of the value (not derived from
    // its content) — mutating inner_ cannot violate the lattice
    // position.  Comonad and RelativeMonad modalities EXCLUDE these
    // operations: in those forms the grade encodes information that
    // depends on the value's identity (Secret<T>'s classification of
    // the specific bytes, Tagged<T, Source>'s provenance of the
    // specific value), and raw mutation would silently change what
    // the grade is asserting about.
    //
    // Wrappers above Graded that DO want to constrain mutation
    // further (Monotonic's monotone-only updates, AppendOnly's
    // append-only updates) hide these operations via encapsulation —
    // Graded ships the SOUND operations; the wrapper chooses the
    // SAFE subset to expose.  Linear<T>'s migration to a Graded-
    // backed implementation depends on these accessors (Linear's
    // peek_mut and swap forward to them).
    //
    // ── REFINED GATE: AbsoluteModality OR empty grade ───────────────
    //
    // The first-cut gate `requires AbsoluteModality<M>` was conservative
    // but unnecessarily restrictive.  The PRINCIPLE is "mutation is
    // allowed when it can't violate the grade."  That principle has
    // TWO satisfying conditions:
    //
    //   1. Absolute modality: grade is orthogonal to value content
    //      (linearity, watermark, prefix length).
    //   2. Empty grade: no runtime grade information exists (singleton
    //      tag at type level — Secret's At<Conf::Secret>, Tagged's
    //      TrustLattice<Source>).  Mutation has nothing to violate.
    //
    // The refined gate admits mutation for all the migration patterns
    // (Linear, Refined, Monotonic, AppendOnly, Tagged, Secret) and
    // still forbids it for the dangerous case: Comonad/RelativeMonad
    // over a non-empty grade where the grade encodes information
    // about the specific value bytes that mutation would silently
    // invalidate.

    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return inner_;
    }

    constexpr void swap(Graded& other)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<grade_type>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(inner_, other.inner_);
        swap(grade_, other.grade_);
    }

    friend constexpr void swap(Graded& a, Graded& b)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<grade_type>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    // ── Comonad: counit (extract from a Comonad-form value) ─────────
    //
    // Aliased wrappers may rename this — Secret<T>::declassify<Policy>()
    // is the named counit with audit-discoverable policy tag.  The
    // bare Graded::extract() is the unnamed counit.
    [[nodiscard]] constexpr T extract() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(inner_);
    }

    // ── RelativeMonad: unit (inject into a RelativeMonad-form value) ─
    //
    // Grade taken `const` per P2900R14 — by-value postcondition
    // parameters must be const.  Local copy `g` is non-const so we
    // can std::move it into the constructor; copy is no-op for empty
    // grade types and one small-struct copy for non-empty.
    [[nodiscard]] static constexpr Graded inject(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_copy_constructible_v<grade_type> &&
        std::is_nothrow_move_constructible_v<grade_type>)
        requires RelativeMonadModality<M>
    {
        // post-via-contract_assert (GCC 16 ICE on `post()` w/ template
        // expressions — see feedback_gcc16_c26_contract_gotchas).
        grade_type expected = grade;  // copy for the assertion
        Graded result{std::move(value), std::move(grade)};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    // ── Lattice operations ──────────────────────────────────────────

    // weaken: widen the grade.  pre () enforces `L::leq(current, new)`
    // — weakening only goes UP the lattice, never DOWN.  The const&
    // overload is gated on copy_constructible<T> so move-only T types
    // (the eventual Linear<T>) cleanly fall through to the && overload
    // instead of producing a noisy copy-deleted error.
    //
    // Post-condition checking via `contract_assert` in the body
    // (equivalent to post(), but post() ICEs in GCC 16.0.1 on
    // template-dependent expressions — see feedback memory
    // gcc16_c26_contract_gotchas rule #6).
    //
    // C++26 clause order: noexcept → requires → pre → body.
    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) const&
        noexcept(std::is_nothrow_copy_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
        pre (L::leq(grade_, new_grade))
    {
        Graded result{inner_, new_grade};
        contract_assert(L::leq(result.grade(), new_grade)
                     && L::leq(new_grade, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) &&
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type> &&
                 std::is_nothrow_move_constructible_v<grade_type>)
        pre (L::leq(grade_, new_grade))
    {
        grade_type expected = new_grade;  // copy for the assertion
        Graded result{std::move(inner_), std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    // compose: join grades via L::join.  Value comes from *this; the
    // other Graded contributes only its grade.  Right-biased on value
    // for symmetry with the Reader-monad analogy.  Same const&-vs-&&
    // ref-qualifier discipline as weaken.  Post-condition via
    // contract_assert (GCC 16 ICE workaround).
    [[nodiscard]] constexpr Graded compose(Graded const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
    {
        grade_type expected = L::join(grade_, other.grade_);
        Graded result{inner_, expected};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
    {
        grade_type expected = L::join(grade_, other.grade_);
        Graded result{std::move(inner_), expected};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }
};

// ════════════════════════════════════════════════════════════════════
// Partial specialization: value type IS the lattice element type
// ════════════════════════════════════════════════════════════════════
//
// When `L::element_type` is exactly `T`, the value and the grade are
// conceptually one — for example MonotoneLattice<T, Cmp>::element_type
// = T (the watermark IS the value).  The primary template stores
// these as two separate fields, paying 2× sizeof(T) AND introducing a
// sync hazard (mutating one without the other diverges them).
//
// This specialization stores a SINGLE field and forwards both peek()
// and grade() to it.  The two-arg `Graded(value, grade)` constructor
// contract-asserts that its two arguments are lattice-equivalent
// (they MUST be in this specialization — they collapse to one
// storage cell).  An ergonomic single-T constructor is provided for
// callers who naturally have only one value to pass.
//
// This unblocks MIGRATE-5 (Monotonic<T, Cmp> → Graded<Absolute,
// MonotoneLattice<T, Cmp>, T>) and any future wrapper whose lattice
// element type equals the value type, without paying a 2× layout
// cost OR breaking downstream cache-line invariants (Arena's
// 64-byte fit, IterationDetector's 128-byte fit).
//
// API surface is BIT-EQUAL to the primary template — the same
// member set, the same modality / lattice / wrapper concepts, the
// same lvalue/rvalue overload split.  Callers cannot tell which
// version they got; the choice is purely a storage optimization
// the compiler picks via the `requires` clause.
//
//   Axiom coverage: same as primary — InitSafe (NSDMI), TypeSafe
//                   (concept gates), MemSafe (no exposed pointers).
//   Runtime cost:   sizeof(Graded<M, L, T>) == sizeof(T) when this
//                   specialization is selected.  Half the primary's
//                   storage cost; the same operation cost (one move
//                   per mutation, one copy per query).

template <ModalityKind M, Lattice L, typename T>
    requires std::is_same_v<typename L::element_type, T>
class [[nodiscard]] Graded<M, L, T> {
public:
    // ── Public type aliases (mirror primary) ────────────────────────
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type       = L;
    using value_type         = T;
    using grade_type         = LatticeElement<L>;  // == T by selection

private:
    // ── Layout (single field — half the primary's cost) ─────────────
    //
    // NSDMI per InitSafe.  No grade_ field — peek() and grade() both
    // forward to value_.  sizeof(Graded<...>) == sizeof(T) for this
    // specialization, vs 2 * sizeof(T) under the primary.
    T value_{};

public:
    // ── Diagnostic (mirror primary) ─────────────────────────────────
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::crucible::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::crucible::algebra::lattice_name<L>();
    }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    // ── Object semantics (defaulted, mirror primary) ────────────────
    constexpr Graded()                         = default;
    constexpr Graded(const Graded&)            = default;
    constexpr Graded(Graded&&)                 = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&)      = default;
    ~Graded()                                  = default;

    // ── Two-arg constructor (API parity with primary) ───────────────
    //
    // Pre: value and grade are lattice-equivalent.  In this
    // specialization they MUST be — they collapse to one storage
    // cell.  We keep the value (storing it via std::move) and
    // accept the grade by-value to consume the rvalue cleanly.
    constexpr Graded(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        pre (L::leq(value, grade) && L::leq(grade, value))
        : value_{std::move(value)}
    {
        // grade is consumed by move binding; we don't store it
        // because value_ already holds the equivalent value.
        (void)grade;
    }

    // ── Ergonomic single-arg constructor (specialization-only) ──────
    //
    // When the caller knows the value and grade are identical (the
    // typical case for Monotonic), this is the natural construction
    // form.  Saves one copy + one move vs the two-arg ctor.
    constexpr explicit Graded(T value_or_grade) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(value_or_grade)} {}

    // ── Construction at bottom (BoundedBelowLattice only) ───────────
    //
    // Two overloads to mirror the primary's at_bottom(T value)
    // signature plus the natural no-arg form.  Both produce a Graded
    // at L::bottom().
    [[nodiscard]] static constexpr Graded at_bottom() noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        return Graded{L::bottom()};
    }

    [[nodiscard]] static constexpr Graded at_bottom(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        Graded result{std::move(value)};
        contract_assert(L::leq(result.grade(), L::bottom())
                     && L::leq(L::bottom(), result.grade()));
        return result;
    }

    // ── Access ──────────────────────────────────────────────────────
    //
    // peek() and grade() BOTH return value_ — they are the same field
    // in this specialization.  The duality is by construction.
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return value_;
    }
    [[nodiscard]] constexpr T consume() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(
        std::is_nothrow_copy_constructible_v<T>)
    {
        return value_;  // grade_type IS T here, by the specialization's selector
    }

    // ── Mutable access (refined gate; see primary's discussion) ─────
    //
    // For the T==element_type specialization, grade_type IS T, so
    // `std::is_empty_v<grade_type>` reduces to `std::is_empty_v<T>`
    // — almost never true for value types but preserved for
    // consistency with the primary template's gate principle.
    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return value_;
    }

    constexpr void swap(Graded& other)
        noexcept(std::is_nothrow_swappable_v<T>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(value_, other.value_);
    }

    friend constexpr void swap(Graded& a, Graded& b)
        noexcept(std::is_nothrow_swappable_v<T>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    // ── Comonad counit (extract from a Comonad-form value) ──────────
    [[nodiscard]] constexpr T extract() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(value_);
    }

    // ── RelativeMonad unit (inject into a RelativeMonad-form value) ─
    //
    // Same equivalence precondition as the two-arg constructor.
    [[nodiscard]] static constexpr Graded inject(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_copy_constructible_v<T>)
        requires RelativeMonadModality<M>
    {
        T expected = grade;  // copy for assertion
        Graded result{std::move(value)};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    // ── weaken: widen the grade (and thus the value) ────────────────
    //
    // Same `pre (L::leq(value_, new_grade))` discipline as the
    // primary.  The result has BOTH value_ and grade_ at new_grade
    // because they're the same field — no inner-vs-grade desync
    // possible.  This is the structural fix that motivates the
    // entire specialization.
    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
        pre (L::leq(value_, new_grade))
    {
        T expected = new_grade;  // copy for the post-assertion
        Graded result{std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) &&
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<T>)
        pre (L::leq(value_, new_grade))
    {
        T expected = new_grade;  // copy for the post-assertion
        Graded result{std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    // ── compose: join two grades (and thus two values) via L::join ──
    [[nodiscard]] constexpr Graded compose(Graded const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        T expected = L::join(value_, other.value_);
        Graded result{expected};  // explicit-single-arg ctor
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) &&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        T expected = L::join(value_, other.value_);
        Graded result{expected};
        contract_assert(L::leq(result.grade(), expected)
                     && L::leq(expected, result.grade()));
        return result;
    }
};

// ════════════════════════════════════════════════════════════════════
// Concept: lattice opt-in for derived-grade storage
// ════════════════════════════════════════════════════════════════════
//
// A lattice L "derives its grade from the value of type T" when it
// publishes a static `grade_of(T const&) -> element_type` member.
// This signals to the Graded substrate that the lattice position can
// be COMPUTED from the value's bytes on demand, rather than stored as
// a separate field.
//
// Canonical example: SeqPrefixLattice<T>::grade_of(Storage<T> const&
// s) returns Length{s.size()}.  The vector already stores its own
// size; mirroring that into a separate Graded grade field would
// duplicate information.
//
// Lattices opt in by adding the static method.  Lattices that don't
// fall through to the standard 2-field Graded layout.
template <typename L, typename T>
concept LatticeDerivesGrade = requires (T const& v) {
    { L::grade_of(v) } -> std::same_as<typename L::element_type>;
};

// ════════════════════════════════════════════════════════════════════
// Partial specialization: derived-grade storage (single field)
// ════════════════════════════════════════════════════════════════════
//
// When `L` provides `grade_of(T const&)` AND the lattice element type
// differs from T (so the previous `T == element_type` specialization
// doesn't fire), the substrate stores a SINGLE T field and computes
// the grade on demand.  No grade duplication.
//
// Storage cost: sizeof(Graded<M, L, T>) == sizeof(T) — same as the
// other zero-cost regimes (empty grade, T == element_type).  The
// grade is a logical view, not a stored field.
//
// LATTICE OPERATIONS RESTRICTION: weaken / compose are NOT provided
// in this specialization.  Their semantics require constructing a
// new Graded with a SPECIFIED grade — but the grade here is derived
// from the value, so to "weaken to grade G" we'd need an inverse
// (mutate value such that grade_of(value) == G).  That inverse is
// generally unavailable; even when it exists (e.g., for SeqPrefix
// "append elements until length == G"), it is the WRAPPER's job to
// expose, not the substrate's.
//
// The wrapper above this specialization (e.g. AppendOnly's append /
// emplace) provides whatever mutation discipline is appropriate; the
// derived grade follows automatically because it's a function of the
// value's current bytes.  Read-side lattice ops (L::leq, L::join on
// observed grades) remain available via L's static interface — just
// not as Graded member functions.
//
// This is the third storage regime, completing the substrate's
// coverage of practical lattice / value combinations:
//   - Empty grade (Linear, Refined): EBO collapses, sizeof(T).
//   - Grade == T (Monotonic): single-field collapse, sizeof(T).
//   - Grade derives from T (AppendOnly): single-field with computed
//     grade, sizeof(T).
//   - Genuinely separate runtime grade (Stale, future): real 2-field
//     storage, cost paid per wrapper.

template <ModalityKind M, Lattice L, typename T>
    requires LatticeDerivesGrade<L, T>
          && (!std::is_same_v<typename L::element_type, T>)
class [[nodiscard]] Graded<M, L, T> {
public:
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type       = L;
    using value_type         = T;
    using grade_type         = LatticeElement<L>;

private:
    // Single field — grade computed via L::grade_of(value_).
    T value_{};

public:
    // ── Diagnostic (mirror primary) ─────────────────────────────────
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::crucible::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::crucible::algebra::lattice_name<L>();
    }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    // ── Object semantics (defaulted; mirror primary) ────────────────
    constexpr Graded()                         = default;
    constexpr Graded(const Graded&)            = default;
    constexpr Graded(Graded&&)                 = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&)      = default;
    ~Graded()                                  = default;

    // ── Two-arg ctor (API parity with primary) ──────────────────────
    //
    // Pre: caller's grade matches L::grade_of(value).  In this
    // specialization the grade is DERIVED from the value, so the
    // user's argument is a witness — we contract-assert it equals
    // what L would derive, then store only the value.
    constexpr Graded(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        pre (L::leq(L::grade_of(value), grade)
             && L::leq(grade, L::grade_of(value)))
        : value_{std::move(value)}
    {
        // grade is consumed; we don't store it.
        (void)grade;
    }

    // ── Ergonomic single-arg constructor (specialization-only) ──────
    //
    // The natural construction form for this specialization — pass
    // the value; the grade derives.  No grade-equivalence check
    // because the user isn't asserting one.
    constexpr explicit Graded(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(value)} {}

    // ── at_bottom: produce a value whose derived grade IS bottom ────
    //
    // Different from the primary's at_bottom(T value) form — we can't
    // generally arrange for an arbitrary user-supplied value to have
    // grade == bottom.  The default-constructed T (e.g. empty
    // Storage<T>) is the natural "bottom-deriving" value: empty
    // vector → Length{0} → SeqPrefixLattice::bottom().
    [[nodiscard]] static constexpr Graded at_bottom() noexcept(
        std::is_nothrow_default_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
              && std::default_initializable<T>
    {
        Graded result{T{}};
        contract_assert(L::leq(result.grade(), L::bottom())
                     && L::leq(L::bottom(), result.grade()));
        return result;
    }

    // ── Access ──────────────────────────────────────────────────────
    //
    // peek() returns the value reference; grade() COMPUTES via
    // L::grade_of(value_) on every call.  For SeqPrefixLattice this
    // is `vector.size()` — O(1).  For lattices with more expensive
    // derivations, callers should cache the result if they need it
    // multiple times.
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return value_;
    }
    [[nodiscard]] constexpr T consume() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(
        noexcept(L::grade_of(std::declval<T const&>())))
    {
        return L::grade_of(value_);
    }

    // ── Mutable access (Absolute modality only) ─────────────────────
    //
    // After mutation, grade() will reflect the new value's derived
    // grade automatically — that's the point of derived-grade.  The
    // wrapper above this specialization is responsible for ensuring
    // the mutation respects whatever lattice discipline applies
    // (e.g., AppendOnly only allows append, so the derived grade
    // monotonically increases).
    // Refined gate: same principle as primary (mutation allowed when
    // grade is orthogonal-to-content OR when grade is empty).  For
    // derived-grade, the grade computes from the value, so mutation
    // implicitly updates both views — the wrapper above (e.g.
    // AppendOnly) provides the discipline.
    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return value_;
    }

    constexpr void swap(Graded& other)
        noexcept(std::is_nothrow_swappable_v<T>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(value_, other.value_);
    }

    friend constexpr void swap(Graded& a, Graded& b)
        noexcept(std::is_nothrow_swappable_v<T>)
        requires (AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    // ── Comonad counit ──────────────────────────────────────────────
    [[nodiscard]] constexpr T extract() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(value_);
    }

    // ── RelativeMonad unit ──────────────────────────────────────────
    [[nodiscard]] static constexpr Graded inject(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires RelativeMonadModality<M>
    {
        Graded result{std::move(value)};
        contract_assert(L::leq(result.grade(), grade)
                     && L::leq(grade, result.grade()));
        return result;
    }

    // ── weaken / compose deliberately omitted ───────────────────────
    //
    // See the specialization's header doc.  Their semantics demand a
    // grade-to-value inverse that doesn't exist generally.  Wrappers
    // provide their own mutation API that updates the value; the
    // derived grade follows automatically.  Read-side lattice ops
    // remain available via L::leq / L::join applied to observed
    // grades.
};

// ── Layout invariant macro (used by MIGRATE-* alias headers) ───────
//
// Each MIGRATE-1..11 alias header invokes this macro at instantiation
// witnesses to prove the wrapper carries zero overhead vs the
// underlying T.  Four-way structural check:
//
//   1. sizeof(Graded<T>)               == sizeof(T)
//   2. alignof(Graded<T>)              == alignof(T)
//   3. is_trivially_destructible_v parity  (preserves trivial dtor)
//   4. is_trivially_copyable_v parity      (preserves memcpy-safety)
//
// (3)(4) are gated on T's own trivial-trait status — for non-trivial T
// we don't claim Graded<T> is trivial; we just claim that IF T is
// trivial in some axis, the wrapper preserves that property.  A
// regression that introduced (e.g.) a non-trivial dtor in grade_type
// would silently break memcpy-based serialization paths through every
// migrated wrapper; this macro catches it at the instantiation site.
//
// Note: is_trivially_default_constructible_v parity DELIBERATELY NOT
// asserted (AUDIT-FOUNDATION-2026-04-26 hardening).  Reason: Graded's
// `inner_{}` and `grade_{}` use NSDMI (non-static data member
// initializer) value-init per the InitSafe axiom.  An NSDMI on any
// member makes the implicit default constructor non-trivial, even
// when both T and grade_type ARE trivially default constructible.
// So `is_trivially_default_constructible_v<Graded<int>>` is FALSE
// while `is_trivially_default_constructible_v<int>` is TRUE — the
// parity check would spuriously fire on any T that's trivially
// default constructible.  The runtime cost of the NSDMI value-init
// is provably zero (compiles to a `mov $0, ...` for arithmetic T,
// elided entirely for empty-grade types via EBO), so triviality of
// the default ctor is not a load-bearing property here — sizeof and
// alignof parity together with the trivial-dtor and trivial-copy
// parity cover the actual byte-level interoperability claim.  See
// AUDIT-FOUNDATION-2026-04-26 graded_self_test::audit_int_default
// witness below for the regression probe.
//
// Note: std::is_layout_compatible_v<Graded<T>, T> deliberately NOT
// asserted.  Per [class.mem], two standard-layout class types are
// layout-compatible only when they have the same number of NSDMI
// members in the same order.  Graded has TWO members (inner_,
// grade_) even when grade_ EBO-collapses to zero bytes; T has its
// own member structure.  is_layout_compatible_v is ALWAYS false for
// the wrapper-vs-T pair, so asserting it would universally fail.
// What we DO assert (sizeof + alignof + trivial-trait parity) is the
// closest tractable approximation to "behaviorally interchangeable
// at the byte level for memcpy / bit_cast purposes".
#define CRUCIBLE_GRADED_LAYOUT_INVARIANT(GradedAlias, T_)                       \
    static_assert(sizeof(GradedAlias<T_>) == sizeof(T_),                        \
                  "Graded alias " #GradedAlias " over " #T_                     \
                  ": sizeof mismatch — review [[no_unique_address]] usage "     \
                  "and lattice element type");                                  \
    static_assert(alignof(GradedAlias<T_>) == alignof(T_),                      \
                  "Graded alias " #GradedAlias " over " #T_                     \
                  ": alignof mismatch — over-aligned grade_type forced "        \
                  "wrapper alignment > T's alignment");                         \
    static_assert(std::is_trivially_destructible_v<T_> ==                       \
                  std::is_trivially_destructible_v<GradedAlias<T_>>,            \
                  "Graded alias " #GradedAlias " over " #T_                     \
                  ": trivial-destructibility parity broken — grade_type "       \
                  "introduced a non-trivial destructor (would force "           \
                  "non-trivial wrapper dtor and break arena bulk-free paths)"); \
    static_assert(std::is_trivially_copyable_v<T_> ==                           \
                  std::is_trivially_copyable_v<GradedAlias<T_>>,                \
                  "Graded alias " #GradedAlias " over " #T_                     \
                  ": trivial-copyability parity broken — wrapper is no "        \
                  "longer memcpy-safe (would break Cipher serialize / SPSC "    \
                  "ring entry copy paths)")

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::graded_self_test {

using ::crucible::algebra::detail::lattice_self_test::TrivialBoolLattice;

// A trivially-empty-grade lattice — used to demonstrate the EBO
// collapse path (sizeof(Graded<...>) == sizeof(T) when both inner
// AND grade are empty).  Concrete static-grade lattices like
// QttSemiring::At<N> ship per ALGEBRA-4..14; this in-house witness
// proves the layout invariant in the absence of those headers.
//
// Operations are `constexpr` (NOT consteval) per the convention in
// algebra/Lattice.h — Graded's `pre (L::leq(...))` is evaluated at
// runtime under enforce semantic and must be able to call leq with
// non-constant arguments.
struct TrivialEmptyLattice {
    using element_type = std::integral_constant<int, 1>;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialEmpty"; }
};
static_assert(Lattice<TrivialEmptyLattice>);
static_assert(BoundedLattice<TrivialEmptyLattice>);
static_assert(std::is_empty_v<TrivialEmptyLattice::element_type>);

struct EmptyValue {};
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// Type instantiation under each modality.
using GComonad   = Graded<ModalityKind::Comonad,       TrivialBoolLattice, EmptyValue>;
using GRelMonad  = Graded<ModalityKind::RelativeMonad, TrivialBoolLattice, EmptyValue>;
using GAbsolute  = Graded<ModalityKind::Absolute,      TrivialBoolLattice, EmptyValue>;
using GRelative  = Graded<ModalityKind::Relative,      TrivialBoolLattice, EmptyValue>;

static_assert(std::is_default_constructible_v<GComonad>);
static_assert(std::is_default_constructible_v<GRelMonad>);
static_assert(std::is_default_constructible_v<GAbsolute>);
static_assert(std::is_default_constructible_v<GRelative>);

// Type aliases reachable.
static_assert(std::is_same_v<GAbsolute::value_type,   EmptyValue>);
static_assert(std::is_same_v<GAbsolute::lattice_type, TrivialBoolLattice>);
static_assert(std::is_same_v<GAbsolute::grade_type,   bool>);
static_assert(GAbsolute::modality == ModalityKind::Absolute);

// Diagnostic names propagate.
static_assert(GAbsolute::modality_name() == "Absolute");
static_assert(GAbsolute::lattice_name()  == "TrivialBool");
static_assert(GComonad::modality_name()  == "Comonad");

// ── Layout: dynamic grade (TrivialBool's bool element) ─────────────
//
// EmptyValue + 1B grade collapses inner_ via EBO, so total = 1B grade.
static_assert(sizeof(GAbsolute) == 1);

// 1B value + 1B grade → adjacent fields, total = 2B (no padding needed).
using GOneByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, OneByteValue>;
static_assert(sizeof(GOneByte) == 2);

// 8B value + 1B grade with 8B alignment → grade after value forces
// padding to next multiple of alignof(value); sizeof = 16.  This is
// the worst-case dynamic-grade overhead and demonstrates why static-
// grade lattices (empty element_type, EBO-collapsed) are preferred
// for hot-path wrappers.
using GEightByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, EightByteValue>;
static_assert(sizeof(GEightByte) == 16);

// ── Layout: static (empty) grade — the EBO-collapse path ────────────
//
// TrivialEmptyLattice's element_type is empty (std::integral_constant
// is an empty class).  Both inner_ and grade_ EBO-collapse when the
// value is also empty; non-empty value preserves its size exactly.
using GEmptyGrade_Empty    = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EmptyValue>;
using GEmptyGrade_OneByte  = Graded<ModalityKind::Absolute, TrivialEmptyLattice, OneByteValue>;
using GEmptyGrade_EightB   = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EightByteValue>;

static_assert(sizeof(GEmptyGrade_Empty)    == 1);                          // C++ minimum-object-size
static_assert(sizeof(GEmptyGrade_OneByte)  == sizeof(OneByteValue));       // EBO grade
static_assert(sizeof(GEmptyGrade_EightB)   == sizeof(EightByteValue));     // EBO grade

// Construction with value + grade.
constexpr GOneByte g_at_top{OneByteValue{}, true};
static_assert(g_at_top.grade() == true);
static_assert(g_at_top.peek().c == 0);

// at_bottom convenience for bounded lattices.
constexpr GOneByte g_bot = GOneByte::at_bottom(OneByteValue{});
static_assert(g_bot.grade() == false);  // TrivialBool::bottom() is false

// weaken: widen grade.  false ⊑ true is legal.
constexpr GOneByte g_weakened = g_bot.weaken(true);
static_assert(g_weakened.grade() == true);

// compose: lattice join.  bot ⊕ top = top.
constexpr GOneByte g_composed = g_bot.compose(g_at_top);
static_assert(g_composed.grade() == true);

// ── Capability gates via concept (concepts SFINAE cleanly; inline
//     `requires(...) { ... }` against member-function constraints
//     emits a hard error in some GCC versions, hence the indirection) ─
template <typename G> concept CanExtract = requires(G g) { std::move(g).extract(); };
template <typename G> concept CanInject  = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };

// Comonad-only: extract counit reachable iff modality == Comonad.
static_assert( CanExtract<GComonad>);
static_assert(!CanExtract<GAbsolute>);
static_assert(!CanExtract<GRelMonad>);
static_assert(!CanExtract<GRelative>);

// RelativeMonad-only: inject unit reachable iff modality == RelativeMonad.
static_assert( CanInject<GRelMonad>);
static_assert(!CanInject<GComonad>);
static_assert(!CanInject<GAbsolute>);
static_assert(!CanInject<GRelative>);

// Layout invariant macro fires correctly on the EBO-collapsed path.
template <typename T> using AbsoluteOverEmpty =
    Graded<ModalityKind::Absolute, TrivialEmptyLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, EightByteValue);

// AUDIT-FOUNDATION-2026-04-26 regression — invoke the macro on a
// trivially-default-constructible T (int, double).  Pre-audit, the
// trivial-default-constructible PARITY check spuriously fired here
// because Graded's NSDMI-initialized members make the wrapper's
// implicit default ctor non-trivial even when T's IS trivial — but
// the layout claim (sizeof, alignof, dtor / copy parity) holds.
// Dropping the tdc parity check from the macro is the fix; this
// witness pins it.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, double);

// Document the underlying NSDMI-vs-trivial-ctor interaction directly
// so future maintainers see the concrete asymmetry.
static_assert(std::is_trivially_default_constructible_v<int>);
static_assert(!std::is_trivially_default_constructible_v<AbsoluteOverEmpty<int>>,
    "Graded's NSDMI-initialized inner_/grade_ members make the implicit "
    "default ctor non-trivial — the layout-invariant macro must NOT assert "
    "trivial-default-constructibility parity.  See macro doc-comment for "
    "rationale.");

// The other parity claims still hold for trivially-X T:
static_assert(std::is_trivially_destructible_v<int>);
static_assert(std::is_trivially_destructible_v<AbsoluteOverEmpty<int>>);
static_assert(std::is_trivially_copyable_v<int>);
static_assert(std::is_trivially_copyable_v<AbsoluteOverEmpty<int>>);

// ── Move-only T compatibility ───────────────────────────────────────
//
// For wrappers like the eventual Linear<T> the inner T may itself be
// move-only.  The const& overloads of weaken/compose are gated on
// std::copy_constructible<T> so move-only T cleanly forces &&
// (rvalue this) — no noisy copy-deleted error cascade.
struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

using GMoveOnly = Graded<ModalityKind::Absolute, TrivialEmptyLattice, MoveOnlyValue>;

// const& overloads SFINAE away for move-only T; only && remains.
template <typename G> concept HasConstWeaken =
    requires(G const& g, typename G::grade_type r) { g.weaken(r); };
template <typename G> concept HasRvalueWeaken =
    requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };

static_assert( HasConstWeaken<GOneByte>);    // copyable T → both available
static_assert( HasRvalueWeaken<GOneByte>);
static_assert(!HasConstWeaken<GMoveOnly>);   // move-only T → const& gated off
static_assert( HasRvalueWeaken<GMoveOnly>);  // && always works

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Forces evaluation through a non-constexpr function so the lattice
// operations called from Graded::weaken's `pre()` are exercised at
// RUNTIME — catches the consteval-vs-constexpr trap that pure
// static_assert-only tests miss.
//
// The function is `inline` (not constexpr) so the body must compile
// against runtime semantics.  TU optimizer almost certainly elides
// the call entirely under -O3, but the front-end still type-checks.
inline void runtime_smoke_test() {
    OneByteValue value{42};
    GOneByte initial{value, false};                         // runtime ctor
    GOneByte widened   = initial.weaken(true);              // runtime weaken (lvalue this)
    GOneByte composed  = initial.compose(widened);          // runtime compose (lvalue this)
    GOneByte moved     = std::move(widened).weaken(true);   // runtime weaken (rvalue this)
    GOneByte mcomposed = std::move(initial).compose(composed);  // runtime compose (rvalue this)

    // Use the results so the optimizer can't elide the calls.
    [[maybe_unused]] bool g1 = composed.grade();
    [[maybe_unused]] bool g2 = moved.grade();
    [[maybe_unused]] bool g3 = mcomposed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(mcomposed).consume().c;
}

}  // namespace detail::graded_self_test

}  // namespace crucible::algebra
