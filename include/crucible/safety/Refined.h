#pragma once

// ── crucible::safety::Refined<Pred, T> ──────────────────────────────
//
// Refinement type: a T paired with a compile-time-named predicate.
//
//   Axiom coverage: InitSafe, NullSafe, TypeSafe (code_guide §II).
//   Runtime cost:   zero on hot path (contract semantic=ignore),
//                   one branch at boundaries (semantic=enforce).
//                   sizeof(Refined<P, T>) == sizeof(T).
//
// - Construction contract-checks the predicate.
// - No implicit conversion to T — use .value() explicitly.  This is
//   load-bearing: a Refined<positive, int> must NOT silently pass to a
//   function taking plain int.
// - Trusted construction (Refined::Trusted{}) for internal paths that
//   have already proven the invariant.
//
// Name every load-bearing predicate.  Don't define anonymous refinements
// at call sites — aliases are what participate in grep and review.

// ── MIGRATED to Graded<Absolute, BoolLattice<Pred>, T>  (#462) ─────
//
// As of MIGRATE-2 (2026-04-26) Refined<Pred, T> is a thin wrapper
// around the algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          BoolLattice<std::remove_cv_t<decltype(Pred)>>,
//          T>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API surface (value() / into() / Trusted{} / comparison
// operators / predicate_type alias / all named-predicate aliases /
// implies_v + predicate_implies machinery).  Storage is delegated to
// Graded; the lattice's element_type is empty (singleton "Pred holds")
// and EBO collapses both grade_ and the wrapper itself, so
// sizeof(Refined<P, T>) == sizeof(T) is preserved by structural
// guarantee, not by hand.
//
// Cross-predicate subsumption stays here (predicate_implies family at
// the bottom of this file) — it is logically an implication-lattice
// over BoolLattice<P> instances, not internal to BoolLattice<Pred>'s
// per-instance structure.  SessionPayloadSubsort.h consumes it via
// the existing is_subsort<Refined<P, T>, Refined<Q, T>> specialization
// gated by implies_v<P, Q>; that hook compiles unchanged because
// Refined is still a class template parameterised on the same
// (auto Pred, typename T).
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/BoolLattice.h>
#include <crucible/safety/Linear.h>

#include <bit>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Common predicates ──────────────────────────────────────────────
//
// User code can define more; each is a stateless lambda or a
// constexpr-callable function object.

inline constexpr auto positive = [](auto x) constexpr noexcept {
    return x > decltype(x){0};
};

inline constexpr auto non_negative = [](auto x) constexpr noexcept {
    return x >= decltype(x){0};
};

// non_zero differs from positive for unsigned types (positive is "> 0",
// which for unsigned is also "!= 0", but explicit non_zero documents
// "sentinel reservation" rather than "signed sign class").
// Works on wrapped strong hash types via `.raw() != 0`, or scalars.
inline constexpr auto non_zero = [](const auto& x) constexpr noexcept {
    if constexpr (requires { x.raw(); })
        return x.raw() != 0;
    else
        return x != decltype(x){0};
};

inline constexpr auto non_null = [](auto* p) constexpr noexcept {
    return p != nullptr;
};

inline constexpr auto power_of_two = [](auto x) constexpr noexcept {
    using U = decltype(x);
    return x != U{0} && (x & (x - U{1})) == U{0};
};

// Non-empty predicate for containers.
inline constexpr auto non_empty = [](const auto& c) constexpr noexcept {
    return !c.empty();
};

// ── Parameterised predicates (struct-template form) ────────────────
//
// Each parameterised predicate is a NAMED struct template with a
// constexpr operator() rather than a `template <...> inline constexpr
// auto` lambda variable.  The struct form is functionally identical
// for callers — `bounded_above<N>(x)` still works — but is structurally
// nameable so the cross-predicate implication trait
// (predicate_implies, below) can partial-specialise on the struct
// shape and recover the template parameters.  Lambda closure types
// are unique unnamed types: a `template <auto N> inline constexpr auto
// p = [](){...};` produces a different unnamed closure type per N
// that the compiler cannot pattern-match back to N via auto NTTP
// deduction (probed; see commit log for #227).
//
// Each predicate ships in two pieces:
//   * `Aligned<N>` / `InRange<L,H>` / `BoundedAbove<M>` / `LengthGe<N>`
//     — the struct template (the *type*).  Specialise predicate_implies
//     on these.
//   * `aligned<N>` / `in_range<L,H>` / `bounded_above<M>` / `length_ge<N>`
//     — the variable template (the *value*).  Use these at call sites
//     and as Refined's NTTP.

template <std::size_t Alignment>
struct Aligned {
    constexpr bool operator()(auto* p) const noexcept {
        return (std::bit_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
    }
};

template <std::size_t Alignment>
inline constexpr Aligned<Alignment> aligned{};

template <auto Lo, auto Hi>
struct InRange {
    constexpr bool operator()(auto x) const noexcept {
        return x >= decltype(x)(Lo) && x <= decltype(x)(Hi);
    }
};

template <auto Lo, auto Hi>
inline constexpr InRange<Lo, Hi> in_range{};

template <auto Max>
struct BoundedAbove {
    constexpr bool operator()(auto x) const noexcept {
        return x <= decltype(x)(Max);
    }
};

template <auto Max>
inline constexpr BoundedAbove<Max> bounded_above{};

// Length-ge predicate for spans / strings / any .size()-having container.
template <std::size_t N>
struct LengthGe {
    constexpr bool operator()(const auto& c) const noexcept {
        return c.size() >= N;
    }
};

template <std::size_t N>
inline constexpr LengthGe<N> length_ge{};

// ── Concept gate: Pred must be invocable on T ──────────────────────
//
// Used on the construction path (NOT on the class template itself).
// Two reasons for this scoping:
//
//   1. The TYPE Refined<positive, Tagged<int, source::Sanitized>>
//      MUST remain nameable for SessionPayloadSubsort.h's type-level
//      subsort axioms (`is_subsort_v<Refined<P, Tagged<T,S>>,
//      Tagged<T,S>>`) — those metafunctions reason over the wrapper's
//      type without ever constructing an instance.  Putting the
//      `requires` on the class template would break the entire
//      subsort discipline.
//
//   2. The CONSTRUCTOR is the operation that actually evaluates
//      Pred(v).  Gating it with `requires` turns the SFINAE-cascade
//      from inside the `pre(Pred(v))` clause into a clean concept-
//      violation message at the call site.  Predicate-vs-T mismatch
//      surfaces as "no matching constructor; Pred not invocable on
//      T" rather than a wall of errors deep in <contracts>.
//
// The concept allows Pred to return either bool directly OR anything
// convertible to bool (existing predicates use both shapes).  Const-
// reference parameter matches the contract's view of the constructed
// value before std::move.
template <auto Pred, typename T>
concept PredicateInvocableOn = requires (T const& v) {
    { Pred(v) } -> std::convertible_to<bool>;
};

// ── The wrapper (Graded-backed per MIGRATE-2 #462) ─────────────────

template <auto Pred, typename T>
class [[nodiscard]] Refined {
public:
    using value_type     = T;
    using predicate_type = decltype(Pred);
    // Lattice carrying the predicate at the type level.  Pred is an
    // auto-NTTP (a value); BoolLattice takes the predicate's TYPE,
    // hence remove_cv_t<decltype(Pred)>.  The remove_cv_t strip
    // matters because inline-constexpr predicate variables are const-
    // qualified at file scope but auto-NTTPs strip the const — same
    // discipline as the predicate_implies specialisations below
    // (probed on GCC 16; see commit log for #227).
    using lattice_type = ::crucible::algebra::lattices::BoolLattice<
        std::remove_cv_t<decltype(Pred)>>;

    // Modality declaration — Round-4 CHEAT-5; see Linear.h for the
    // rationale.  Refined is Absolute (predicate is a static type-
    // level property, not a monadic structure).
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // Public per GRADED-TRAIT-1 — see Linear.h for the rationale.
    using graded_type = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

public:

    // Tag for skipping the predicate check.  Use only when the caller
    // has already proven the invariant (internal paths, re-wrapping
    // already-validated boundary data).
    struct Trusted {};

    // Checked construction — contract fires if the predicate fails.
    // `requires PredicateInvocableOn<Pred, T>` upgrades a Pred(T)
    // invocability mismatch from a contract-clause SFINAE wall into
    // a clean concept-violation diagnostic at the call site.  Type-
    // level uses (e.g. SessionPayloadSubsort axioms) are unaffected
    // because they never instantiate the constructor.
    constexpr explicit Refined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
        pre(Pred(v))
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // Trusted construction — no check, caller-asserted invariant.
    // No PredicateInvocableOn requirement — the Trusted path explicitly
    // bypasses the predicate, so even non-invocable Pred is admissible
    // (the caller takes responsibility for the invariant).
    constexpr Refined(T v, Trusted) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // Refinement applies to the value; once constructed the invariant
    // holds.  Copy/move just preserve the value (defaulted from
    // graded_type, which is itself defaulted).
    Refined(const Refined&)            = default;
    Refined(Refined&&)                 = default;
    Refined& operator=(const Refined&) = default;
    Refined& operator=(Refined&&)      = default;

    // Explicit accessor — no implicit conversion.  Forwards to
    // Graded::peek() under the hood.
    [[nodiscard]] constexpr const T& value() const noexcept {
        return impl_.peek();
    }

    // Explicit raw extraction for re-wrapping paths.  Forwards to
    // Graded::consume() — rvalue-this consumes the inner value.
    [[nodiscard]] constexpr T into() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // Equality / ordering on the underlying value.  Refined values of
    // the same Pred and T compare by their inner T (forwarded through
    // Graded::peek()).
    friend constexpr bool operator==(const Refined& a, const Refined& b)
        noexcept(noexcept(a.impl_.peek() == b.impl_.peek()))
    {
        return a.impl_.peek() == b.impl_.peek();
    }

    friend constexpr auto operator<=>(const Refined& a, const Refined& b)
        noexcept(noexcept(a.impl_.peek() <=> b.impl_.peek()))
        requires std::three_way_comparable<T>
    {
        return a.impl_.peek() <=> b.impl_.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13)
    // — answers "what type does this Refined wrap?" without requiring
    // the caller to inspect the predicate signature.
    //
    // lattice_name(): "BoolLattice<Pred>" (the trivial-bool refinement
    // lattice).  The actual stringification of Pred depends on
    // BoolLattice's own consteval name() routine.
    //
    // Audit-Tier-2 cross-wrapper parity sweep — every migrated wrapper
    // (Linear, Refined, Tagged, Secret, Monotonic, AppendOnly, Stale,
    // TimeOrdered) ships these two forwarders at the bottom of its
    // class body so review-time greps always find them in the same
    // place.  Symmetry is the property; concrete strings vary.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// Zero-cost guarantee: a Refined is exactly its underlying T.
static_assert(sizeof(Refined<positive,    int>)    == sizeof(int));
static_assert(sizeof(Refined<non_null,    void*>)  == sizeof(void*));
static_assert(sizeof(Refined<power_of_two,std::size_t>) == sizeof(std::size_t));

// Zero-cost guarantee for the parameterised struct-template predicates
// (refactored from inline-constexpr-auto lambdas to enable
// predicate_implies partial specialisation; #227).  Each Aligned<N>
// / InRange<L,H> / BoundedAbove<M> / LengthGe<N> is an empty class
// (no data members), so [[no_unique_address]]-equivalent EBO collapses
// the predicate's footprint to zero inside Refined<Pred, T>.
static_assert(sizeof(Refined<aligned<64>,         void*>) == sizeof(void*));
static_assert(sizeof(Refined<bounded_above<1024u>, int>)   == sizeof(int));
static_assert(sizeof(Refined<in_range<0, 100>,    int>)   == sizeof(int));
static_assert(sizeof(Refined<length_ge<1>,        void*>) == sizeof(void*));

// ── §XXI Universal Mint factory — fixy-A1-005 (#1547) ──────────────
//
// `mint_refined<Pred, T>(value)` synthesizes an authoritative
// `Refined<Pred, T>` at the §XXI grep-discoverable boundary.  Per
// CLAUDE.md §XXI: every cross-tier composition factory is named
// `mint_<noun>` so `grep "mint_"` finds every authorization point.
// Constructing `Refined<Pred, T>{value}` directly bypasses the §XXI
// grep — production code admitting a value into the refinement
// type-system MUST route through this factory.
//
// HS14 gate: `PredicateInvocableOn<Pred, T>` is the load-bearing
// soundness check (same concept the class ctor uses).  The concept
// gates BOTH (a) Pred is callable on `T const&` AND (b) Pred's
// return type is convertible to bool — so a category-error
// predicate (wrong argument type) and a return-shape predicate
// (void / non-bool-convertible) both fail at the concept boundary
// with a clean diagnostic, not a wall of contract-clause SFINAE.
// Two HS14 neg-compile fixtures at test/safety_neg/ witness both
// failure modes:
//   * predicate-arg-mismatch  — Pred not invocable on T
//   * predicate-return-mismatch — Pred returns non-bool-convertible
//
// Template parameter order: `<Pred, T>` — Pred explicit (an auto-
// NTTP), T deduced from the argument.  Mirrors the
// `Refined<Pred, T>` class declaration's NTTP-first convention.
//
// Hot-path cost: zero — `[[nodiscard]] constexpr noexcept`, EBO
// collapses the Graded substrate.  Identical machine code to a
// raw `Refined<Pred, T>{std::move(value)}` ctor call under -O3,
// modulo the contract evaluation semantic of the active TU.

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr Refined<Pred, T> mint_refined(T value)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    return Refined<Pred, T>{std::move(value)};
}

// ── Common refinement aliases ──────────────────────────────────────
//
// Named so they participate in grep/review and don't drift to
// anonymous Refined<positive, T> at every call site (per code_guide
// §XVI: "Every load-bearing predicate gets a named alias").

template <typename T> using NonNull       = Refined<non_null, T>;
template <typename T> using Positive      = Refined<positive, T>;
template <typename T> using NonNegative   = Refined<non_negative, T>;
template <typename T> using PowerOfTwo    = Refined<power_of_two, T>;
template <typename T> using NonZero       = Refined<non_zero, T>;
template <typename T> using NonEmpty      = Refined<non_empty, T>;

// CLAUDE.md §XVI canonical example: "every load-bearing predicate gets
// a named alias — PositiveInt, NonNullTraceEntry, ValidSlotId,
// NonEmptySpan<T>".  NonEmptySpan<T> uses length_ge<1> rather than
// non_empty because std::span exposes .size() uniformly across all
// instantiations (size_type is std::size_t for every T), and
// length_ge participates in the predicate_implies subsort lattice
// (LengthGe<N> ⇒ LengthGe<M> when N ≥ M), so a NonEmptySpan<T> can
// strengthen to MinLengthSpan<N, T> for N > 1 without re-validating.
// non_empty is a simpler `!c.empty()` lambda — useful for containers
// where .size() is not O(1) (rare in Crucible), but redundant here.
template <typename T> using NonEmptySpan  = Refined<length_ge<std::size_t{1}>, std::span<T>>;

// ── FIXY-U-160 — parameterised §XVI named aliases ───────────────────
//
// `MinLength<N, T>` and `MaxBounded<Max, T>` close the §XVI alias-
// discipline gap for the *parameterised* predicate families that the
// unparameterised aliases above (NonEmptySpan / NonEmpty / NonNegative)
// only cover at fixed parameter values.
//
//   MinLength<N, T>     = Refined<length_ge<N>, T>
//   MaxBounded<Max, T>  = Refined<bounded_above<Max>, T>
//
// Why named aliases (not anonymous `Refined<length_ge<N>, T>` at
// call sites): the §XVI discipline rule is "every load-bearing
// predicate gets a named alias".  Production has 142+ inline
// `Refined<bounded_above<...>>` sites (e.g. CKernel-2/SchemaTab-1/
// Transaction-5 sat-counter family, see
// feedback_bounded_monotonic_counter_pattern.md) and a smaller but
// growing population of `Refined<length_ge<N>, std::span<T>>` sites.
// A named alias gives reviewers a grep target and lets future
// hardening (e.g. lifting a span field from `Refined<length_ge<8>>`
// to `MinLength<8>` at signature level) propagate by one rename.
//
// The alias inherits the same `PredicateInvocableOn<Pred, T>` concept
// gate as the bare Refined<>; substitution failure at the alias site
// surfaces the canonical PredicateInvocableOn diagnostic, not an
// SFINAE wall inside the contract clause (mirrors NonZero/NonEmpty
// from FIXY-U-159 — see neg_refined_nonempty_alias_rejects_arithmetic
// + neg_refined_nonzero_alias_rejects_no_compare_struct).
//
// Subsort participation: MinLength<N, T> strengthens to MinLength<M, T>
// for any M ≤ N (per LengthGe<N> ⇒ LengthGe<M> partial spec above),
// and MinLength<N, T> for N ≥ 1 strengthens to NonEmpty<T> (per the
// LengthGe<N> ⇒ non_empty bridge from FIXY-U-159b).  MaxBounded
// strengthens via BoundedAbove<N> ⇒ BoundedAbove<M> when N ≤ M.
// Production code receiving `MaxBounded<7>` accepts a `MaxBounded<3>`
// caller through SessionPayloadSubsort's is_subsort fold.
//
// Soundness gate: N ≥ 1 on MinLength is NOT enforced at the alias
// because the underlying LengthGe<N> primitive accepts N=0 (vacuous
// — c.size() >= 0 always).  N=0 is intentionally permitted as a
// degenerate-but-well-formed alias for "any-length container", and
// the predicate_implies axiom `length_ge<0> ⇒ non_empty` is correctly
// REJECTED by the soundness witness (see U-159b static_asserts).
template <std::size_t N, typename T> using MinLength  = Refined<length_ge<N>, T>;
template <auto Max, typename T>      using MaxBounded = Refined<bounded_above<Max>, T>;

// ── FIXY-U-161 — closing §XVI parameterised-alias surface ───────────
//
// Two more parameterised predicate families lacked named aliases
// before this ship — `aligned<N>` (pointer-alignment) and
// `in_range<L, H>` (closed-interval value bound).  After this ship,
// the §XVI "every load-bearing predicate gets a named alias" rule
// holds for every parameterised predicate in this header.
//
//   AlignedTo<N, T>          = Refined<aligned<N>, T>
//   WithinRange<L, H, T>     = Refined<in_range<L, H>, T>
//
// AlignedTo<N, T> uses `aligned<N>` which takes `auto* p` — T MUST be
// a pointer type (or a type implicitly convertible to a pointer via
// the lambda's auto* deduction; reference types and value types are
// rejected at the concept gate).  The body bit_casts p to uintptr_t
// and tests `(addr & (N-1)) == 0`, so N is implicitly assumed to be a
// power of two; if N is not a power of two the alias still compiles
// (the body becomes an arbitrary low-bit mask) — alias-level rejection
// of non-power-of-two N is out of scope for U-161 and would require
// adding a separate compile-time gate.  Production callers should pin
// the alignment to a hardware power-of-two (64 for cache-line, 4096
// for page, etc.).
//
// WithinRange<L, H, T> uses `in_range<L, H>` whose body is
// `x >= L && x <= H` — closed interval, BOTH endpoints inclusive.
// T must support comparison against the deduced types of L and H —
// `operator>=` AND `operator<=` against the NTTPs' deduced types.
// This is structurally stricter than MaxBounded (single operator<);
// a struct that has only one half of the ordering interface (say
// operator<= but not operator>=) compiles for MaxBounded but not for
// WithinRange.  The neg-compile fixture for this fires at the
// dual-operator level.
//
// Inverted-bound case (L > H): the alias still compiles; the predicate
// body evaluates to a vacuous-false (no value can be both >= L and
// <= H when L > H).  Construction-site contract violation fires at
// runtime instead of compile-time — alias-level rejection of L > H
// is out of scope (would require a NTTP-level static_assert which
// belongs at the predicate-struct level not the alias level).
//
// Subsort propagation (existing axioms suffice):
//   * AlignedTo<N> strengthens to AlignedTo<M> via Aligned<N>⇒Aligned<M>
//     (N ≥ M ∧ N mod M = 0) — cache-line-aligned ⇒ word-aligned, etc.
//   * WithinRange<L1, H1> strengthens to WithinRange<L2, H2> via
//     InRange<L1, H1>⇒InRange<L2, H2> (L2 ≤ L1 ∧ H1 ≤ H2).
//   * WithinRange<L, H> ⇒ MaxBounded<H> via the existing
//     InRange⇒BoundedAbove specialisation.
//   * New bridge (added below): WithinRange<L, H> ⇒ NonNegative when
//     L ≥ 0 — closes the gap from the parameterised in_range surface
//     to the unparameterised non_negative surface.
template <std::size_t N, typename T> using AlignedTo   = Refined<aligned<N>, T>;
template <auto Lo, auto Hi, typename T>
using WithinRange = Refined<in_range<Lo, Hi>, T>;

// ── Composition with Linear ─────────────────────────────────────────
//
// Two orthogonal orderings compose Linear and Refined, and they mean
// different things:
//
//   LinearRefined<Pred, T> = Linear<Refined<Pred, T>>
//     The VALUE is refined; the WRAPPER is linear.  Use when the
//     predicate is about the underlying T (e.g. fd >= 0, ptr != null,
//     tag < MAX) and ownership must be single-consumer.  This is the
//     common case — most resources are "handle to a value that
//     satisfies an invariant".
//     Construction checks Pred on T at build time; thereafter the
//     invariant is frozen by the type.  .consume() && yields the
//     Refined<Pred, T>; caller may then .value() or .into().
//
//   RefinedLinear<Pred, T> = Refined<Pred, Linear<T>>
//     The WRAPPER is refined; the inner VALUE is linear.  Use when
//     the predicate is about the Linear<> state itself (e.g. "not
//     moved-from", "holding the exclusive lock") rather than the
//     underlying T.  Rare in practice; most predicates care about
//     the value, not the wrapper.
//
// Named aliases keep the ordering intentional.  Pick the one whose
// predicate-target matches your invariant; don't freely reorder.

template <auto Pred, typename T>
using LinearRefined = Linear<Refined<Pred, T>>;

template <auto Pred, typename T>
using RefinedLinear = Refined<Pred, Linear<T>>;

// Zero-cost: Linear<Refined<P, T>> is exactly sizeof(T) — Linear is
// zero-overhead, Refined is zero-overhead, both storage-transparent.
static_assert(sizeof(LinearRefined<non_null, void*>) == sizeof(void*),
              "LinearRefined must collapse to sizeof(T)");

// Zero-cost: new aliases (NonZero / NonEmpty / NonEmptySpan) must
// collapse to sizeof(T) exactly like the four existing aliases above.
// Witnesses that the alias machinery does NOT introduce overhead vs
// the underlying Refined<Pred, T> — required by CLAUDE.md §XVI claim
// "sizeof(Wrapper<T>) == sizeof(T) under -O3".  Different T per alias
// to cover representative shapes: raw scalar (NonZero), .empty()-having
// container (NonEmpty over std::span — since span has both .empty()
// AND .size(), it admits both predicates), and parameterised
// length_ge<1> over span (NonEmptySpan).
static_assert(sizeof(NonZero<int>)              == sizeof(int));
static_assert(sizeof(NonEmpty<std::span<int>>)  == sizeof(std::span<int>));
static_assert(sizeof(NonEmptySpan<int>)         == sizeof(std::span<int>));

// ═════════════════════════════════════════════════════════════════════
// ── implies_v<P, Q> — cross-predicate implication trait  (#227) ────
// ═════════════════════════════════════════════════════════════════════
//
// implies_v<P, Q> = true reads "every value satisfying predicate P
// also satisfies predicate Q" — i.e. P is at least as STRONG as Q,
// P's truth-set ⊆ Q's truth-set.  Example: `positive` ⇒ `non_negative`
// because every x > 0 also has x ≥ 0.
//
// Consumed by SessionPayloadSubsort.h, which installs the
// strengthening axiom
//
//   is_subsort<Refined<P, T>, Refined<Q, T>>  when  implies_v<P, Q>
//
// so that Send / Recv positions carrying refined payloads compose
// uniformly across predicate strength — Send<Refined<positive, int>,
// K> becomes a subtype of Send<Refined<non_negative, int>, K> for
// free.  See misc/24_04_2026_safety_integration.md §7, §22.
//
// ─── Routing: type-level first, value-level wrapper ────────────────
//
// Specialise the TYPE-level `predicate_implies<PType, QType>` template,
// not `implies_v<auto P, auto Q>` directly.  auto NTTP deduction does
// not recover template parameters of the bound value's type (probed on
// GCC 16) — a partial specialisation `implies_v<Foo<N>{}, Foo<M>{}>`
// with N, M as template parameters fails with "template parameters
// not deducible in partial specialisation".  Pattern-matching works on
// types, so the value-level trait just forwards through decltype.
//
// Convention: specialisations use `std::remove_cv_t<decltype(p)>`
// because inline constexpr variables are const-qualified in the
// enclosing scope while auto NTTPs strip the const (confirmed by
// probing std::is_same_v<decltype(NTTP), decltype(outer-constexpr)> ==
// false, but ...remove_cv_t<> == true).
//
// Defaults to false.  Reflexivity (P ⇒ P) is NOT installed by the
// primary template — is_subsort's reflexivity already comes from the
// std::is_same fall-through in SessionSubtype.h, and duplicating it
// here would invite drift.
//
// ─── User-extensibility ────────────────────────────────────────────
//
// User code that defines its own predicate and wants to participate
// in the implication lattice specialises predicate_implies on the
// predicate's TYPE (struct or closure).  One-liner per pair.

template <typename PType, typename QType>
struct predicate_implies : std::false_type {};

template <auto P, auto Q>
inline constexpr bool implies_v =
    predicate_implies<decltype(P), decltype(Q)>::value;

// ── Unparameterised-lambda implications ────────────────────────────
//
// positive ⇒ non_negative   (∀x. x > 0 ⇒ x ≥ 0)
// positive ⇒ non_zero       (∀x. x > 0 ⇒ x ≠ 0)
// power_of_two ⇒ non_zero   (the definition requires x ≠ 0)

template <>
struct predicate_implies<
    std::remove_cv_t<decltype(positive)>,
    std::remove_cv_t<decltype(non_negative)>>
    : std::true_type {};

template <>
struct predicate_implies<
    std::remove_cv_t<decltype(positive)>,
    std::remove_cv_t<decltype(non_zero)>>
    : std::true_type {};

template <>
struct predicate_implies<
    std::remove_cv_t<decltype(power_of_two)>,
    std::remove_cv_t<decltype(non_zero)>>
    : std::true_type {};

// ── FIXY-U-159b — propagation closure for new alias surface ────────
//
// non_null ⇔ non_zero  (bidirectional, for pointer T)
//
// For raw pointer T*: `non_null(p) = (p != nullptr)` (lambda body
// `p != nullptr`) and `non_zero(p) = (p != decltype(p){0})` (else-
// branch of the requires-dispatch, since pointer-T has no `.raw()`).
// `decltype(p){0}` for pointer T is `(T*)nullptr` so the two
// predicates evaluate to literally identical machine code on
// pointers.  Bidirectional is structurally correct AND closes the
// `NonNull<T*>` vs `NonZero<T*>` alias-pair gap: production code
// that gates on one accepts the other through the subsort axiom in
// SessionPayloadSubsort.h.
//
// The bidirectional axiom is restricted to pointer T at the use
// site by PredicateInvocableOn — `Refined<non_null, int>` is
// ill-formed (non_null's `auto*` parameter rejects scalar int), so
// `is_subsort<Refined<non_zero, int>, Refined<non_null, int>>` is
// vacuously safe (the RHS type doesn't exist for non-pointer T).

template <>
struct predicate_implies<
    std::remove_cv_t<decltype(non_null)>,
    std::remove_cv_t<decltype(non_zero)>>
    : std::true_type {};

template <>
struct predicate_implies<
    std::remove_cv_t<decltype(non_zero)>,
    std::remove_cv_t<decltype(non_null)>>
    : std::true_type {};

// ── Parameterised-predicate implications (type-level partial specs) ─
//
// These work because we refactored the parameterised predicates to
// struct templates above (Aligned<N> / InRange<L,H> / BoundedAbove<M>
// / LengthGe<N>).  auto-NTTP deduction recovers the structural type,
// and the partial spec pattern-matches on it.
//
// Aligned<N> ⇒ Aligned<M>   when N ≥ M ∧ N mod M = 0
//   (e.g. 64-byte aligned implies 32-byte aligned)
template <std::size_t N, std::size_t M>
    requires (N >= M && M > 0 && (N % M == 0))
struct predicate_implies<Aligned<N>, Aligned<M>> : std::true_type {};

// BoundedAbove<N> ⇒ BoundedAbove<M>   when N ≤ M
//   (smaller ceiling implies larger ceiling; e.g. x ≤ 8 ⇒ x ≤ 16)
template <auto N, auto M>
    requires (N <= M)
struct predicate_implies<BoundedAbove<N>, BoundedAbove<M>> : std::true_type {};

// InRange<L1, H1> ⇒ InRange<L2, H2>   when L2 ≤ L1 ∧ H1 ≤ H2
//   (tighter range implies looser range)
template <auto L1, auto H1, auto L2, auto H2>
    requires (L2 <= L1 && H1 <= H2)
struct predicate_implies<InRange<L1, H1>, InRange<L2, H2>> : std::true_type {};

// InRange<L, H> ⇒ BoundedAbove<H>
//   (range ceiling is an upper bound)
template <auto L, auto H>
struct predicate_implies<InRange<L, H>, BoundedAbove<H>> : std::true_type {};

// LengthGe<N> ⇒ LengthGe<M>   when N ≥ M
//   (longer-than-N implies longer-than-M for M ≤ N)
template <std::size_t N, std::size_t M>
    requires (N >= M)
struct predicate_implies<LengthGe<N>, LengthGe<M>> : std::true_type {};

// ── FIXY-U-159b — parameterised ⇒ unparameterised bridge ───────────
//
// LengthGe<N> ⇒ non_empty   for N ≥ 1
//
// STL container invariant [container.reqmts]: `c.empty() ==
// (c.size() == 0)`.  When N ≥ 1, `c.size() >= N` implies
// `c.size() >= 1` which implies `!c.empty()`.  Bridges the
// parameterised `length_ge<N>`-backed alias surface (NonEmptySpan<T>
// from FIXY-U-159, MinLength<N, T> on the U-160 backlog) to the
// unparameterised `non_empty`-backed `NonEmpty<T>` alias through
// the implication lattice — a NonEmptySpan<T> structurally
// strengthens to a NonEmpty<std::span<T>> at any session position.
//
// N=0 is INTENTIONALLY excluded by the `requires (N >= 1)` clause:
// `length_ge<0>(c) = (c.size() >= 0)` is trivially true (size_t is
// unsigned, comparison with 0 always succeeds), so an empty
// container satisfies length_ge<0> but NOT non_empty.  Asserting
// `length_ge<0> ⇒ non_empty` would be UNSOUND — the requires-clause
// is load-bearing soundness, not decoration.
template <std::size_t N>
    requires (N >= 1)
struct predicate_implies<
    LengthGe<N>,
    std::remove_cv_t<decltype(non_empty)>>
    : std::true_type {};

// ── FIXY-U-161 — parameterised ⇒ unparameterised bridge ────────────
//
// InRange<L, H> ⇒ non_negative   for L ≥ 0
//
// `in_range<L, H>(x)` evaluates to `x >= L && x <= H`.  When L ≥ 0,
// the lower-bound conjunct gives x ≥ L ≥ 0 → x ≥ 0 → non_negative(x).
// Bridges the parameterised `in_range<L, H>`-backed alias surface
// (`WithinRange<L, H, T>` from U-161 below) to the unparameterised
// `non_negative`-backed `NonNegative<T>` alias through the implication
// lattice — a `WithinRange<0, 100, int>` structurally strengthens to a
// `NonNegative<int>` at any session position via SessionPayloadSubsort.
//
// L < 0 is INTENTIONALLY excluded by the `requires (L >= 0)` clause:
// `in_range<-5, 100>(x) = (x >= -5 && x <= 100)` admits negative
// values down to -5, which is NOT non_negative.  Asserting
// `in_range<-5, 100> ⇒ non_negative` would be UNSOUND.  The requires-
// clause is load-bearing soundness, not decoration (mirrors U-159b's
// `requires (N >= 1)` discipline on the length_ge bridge).
//
// auto-NTTP discipline: L is captured by `auto` so its TYPE is
// preserved per-instantiation.  The `L >= 0` test works for both
// signed and unsigned NTTPs (unsigned is always ≥ 0; signed is
// checked at compile time).
template <auto L, auto H>
    requires (L >= 0)
struct predicate_implies<
    InRange<L, H>,
    std::remove_cv_t<decltype(non_negative)>>
    : std::true_type {};

// ── FIXY-U-159b — closure axioms (verify the propagation fires) ────
//
// Witness that the new implications are reachable through implies_v
// at file scope — catches a future refactor that drops a partial
// specialisation or breaks the auto-NTTP type-recovery convention.
// Mirrors the static_assert(sizeof(...)) discipline of the alias
// zero-cost guarantees above.

static_assert(implies_v<non_null, non_zero>,
    "FIXY-U-159b: non_null ⇒ non_zero (pointer non-null ≡ pointer non-zero).");
static_assert(implies_v<non_zero, non_null>,
    "FIXY-U-159b: non_zero ⇒ non_null (bidirectional for pointer T).");
static_assert(implies_v<length_ge<1>, non_empty>,
    "FIXY-U-159b: length_ge<1> ⇒ non_empty (size ≥ 1 ⇒ !empty per STL).");
static_assert(implies_v<length_ge<8>, non_empty>,
    "FIXY-U-159b: length_ge<N> ⇒ non_empty for any N ≥ 1.");
static_assert(!implies_v<length_ge<0>, non_empty>,
    "FIXY-U-159b: length_ge<0> is vacuous; must NOT imply non_empty "
    "(soundness — empty container satisfies length_ge<0> but not non_empty).");

// Transitive closure witnesses — the new axioms compose with the
// pre-existing LengthGe<N> ⇒ LengthGe<M> (N ≥ M) chain.  A length_ge<8>
// container is length_ge<1> is non_empty.  The implication lattice
// is structural; transitivity is provided by SessionSubtype's
// is_subsort fold, not by predicate_implies itself, so we witness
// each hop directly.
static_assert(implies_v<length_ge<8>, length_ge<1>>,
    "FIXY-U-159b: length_ge transitivity hop (parameterised pair).");

// ── FIXY-U-161 — closure axioms for in_range ⇒ non_negative bridge ─
//
// Witness propagation at L ≥ 0 cardinalities and the soundness gate
// at L < 0 (must NOT imply non_negative when L is negative).
static_assert(implies_v<in_range<0, 100>, non_negative>,
    "FIXY-U-161: in_range<0, 100> ⇒ non_negative (L ≥ 0 lower bound).");
static_assert(implies_v<in_range<5, 100>, non_negative>,
    "FIXY-U-161: in_range<5, 100> ⇒ non_negative (positive lower bound).");
static_assert(implies_v<in_range<0u, 255u>, non_negative>,
    "FIXY-U-161: unsigned NTTP carries non_negative trivially.");
static_assert(!implies_v<in_range<-5, 100>, non_negative>,
    "FIXY-U-161: in_range<-5, 100> admits negative values; "
    "must NOT imply non_negative (soundness — the L ≥ 0 requires "
    "clause is load-bearing, not decoration).");

// Transitivity hop into pre-existing axioms: in_range<5, 100> ⇒
// in_range<0, 200> (via tighter→looser axiom) ⇒ non_negative (via
// L=0 ≥ 0 bridge).  Direct hop witnessed; transitive chain is fold
// in SessionPayloadSubsort.
static_assert(implies_v<in_range<5, 100>, in_range<0, 200>>,
    "FIXY-U-161: InRange tighter ⇒ looser (precondition for "
    "transitive chain to non_negative through the L=0 bridge).");

namespace detail::refined_self_test {

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercise the construction / accessor / consume / comparison /
// trusted-bypass / mint factory surfaces with non-constant input
// per feedback_algebra_runtime_smoke_test_discipline.
inline void runtime_smoke_test() {
    int seed = 42;                                            // non-constant

    // Checked construction — pre(positive(seed)) must hold.
    Refined<positive, int> p{seed};
    if (p.value() != 42) std::abort();

    // Mint forwarder.
    auto pm = mint_refined<positive, int>(seed);
    if (pm.value() != 42) std::abort();

    // Trusted-bypass — skip predicate, caller-asserted invariant.
    int sentinel = -1;  // would fail positive(); Trusted{} admits.
    Refined<positive, int> tp{sentinel, Refined<positive, int>::Trusted{}};
    if (tp.value() != -1) std::abort();

    // Comparison.
    Refined<positive, int> p2{seed};
    if (!(p == p2)) std::abort();
    Refined<positive, int> p3{seed + 1};
    if ((p <=> p3) != std::strong_ordering::less) std::abort();

    // .into() consumes — yields raw int.
    int extracted = std::move(p).into();
    if (extracted != 42) std::abort();

    // Parameterised predicates.
    Refined<bounded_above<128u>, unsigned int> ba{static_cast<unsigned int>(seed)};
    if (ba.value() != 42u) std::abort();

    Refined<in_range<0, 100>, int> ir{seed};
    if (ir.value() != 42) std::abort();

    // LengthGe over a span-able container.
    int arr[3] = {1, 2, 3};
    std::span<int> sp{arr};
    Refined<length_ge<1>, std::span<int>> ls{sp};
    if (ls.value().size() != 3) std::abort();

    // LinearRefined composition — Linear<Refined<P, T>>.
    LinearRefined<positive, int> lr{Refined<positive, int>{seed}};
    if (lr.peek().value() != 42) std::abort();
    int lr_extracted = std::move(lr).consume().into();
    if (lr_extracted != 42) std::abort();

    // FIXY-U-159 — new alias surfaces exercised through the same
    // checked / smoke discipline as the four pre-existing aliases.
    //
    // NonZero<int> — uses the unparameterised `non_zero` lambda
    // (else-branch fallback, `x != decltype(x){0}`).  Non-zero
    // sentinel constructs cleanly; zero would fire the contract.
    NonZero<int> nz{seed};
    if (nz.value() != 42) std::abort();

    // NonEmpty<std::span<int>> — uses the unparameterised
    // `non_empty` lambda (`!c.empty()`).  std::span<int> has
    // .empty() per [span.obs]; non-empty span constructs.
    NonEmpty<std::span<int>> ne{sp};
    if (ne.value().size() != 3) std::abort();

    // NonEmptySpan<int> — the §XVI canonical alias for a span with
    // ≥1 element.  Uses parameterised `length_ge<1>` so the value
    // composes through predicate_implies (LengthGe<N> ⇒ LengthGe<M>
    // when N ≥ M) — strengthening a NonEmptySpan to MinLengthSpan<N>
    // for N > 1 is a type-level promotion via the existing implies
    // axiom, not a re-validation.
    NonEmptySpan<int> nes{sp};
    if (nes.value().size() != 3) std::abort();
}

}  // namespace detail::refined_self_test

} // namespace crucible::safety
