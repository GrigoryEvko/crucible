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

// ── DEPRECATION-ON-MIGRATE (Phase 2a Graded refactor) ──────────────
// Folds into a Graded<Modality, Lattice, T> alias once safety/Graded.h
// ships (misc/25_04_2026.md §2.3).  Public API preserved; this
// standalone implementation is removed at migration.
//
//   template <typename Pred, typename T>
//   using Refined = Graded<Absolute, BoolLattice<Pred>, T>;
//
// implies_v<P, Q> (#227) lifts to lattice ⊑ on BoolLattice products.
// Do not extend with new specializations — extend the Graded algebra.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/safety/Linear.h>

#include <compare>
#include <cstdint>
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
        return (reinterpret_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
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

// ── The wrapper ────────────────────────────────────────────────────

template <auto Pred, typename T>
class [[nodiscard]] Refined {
    T value_;

public:
    using value_type     = T;
    using predicate_type = decltype(Pred);

    // Tag for skipping the predicate check.  Use only when the caller
    // has already proven the invariant (internal paths, re-wrapping
    // already-validated boundary data).
    struct Trusted {};

    // Checked construction — contract fires if the predicate fails.
    constexpr explicit Refined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(Pred(v))
        : value_{std::move(v)} {}

    // Trusted construction — no check, caller-asserted invariant.
    constexpr Refined(T v, Trusted) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)} {}

    // Refinement applies to the value; once constructed the invariant
    // holds.  Copy/move just preserve the value.
    Refined(const Refined&)            = default;
    Refined(Refined&&)                 = default;
    Refined& operator=(const Refined&) = default;
    Refined& operator=(Refined&&)      = default;

    // Explicit accessor — no implicit conversion.
    [[nodiscard]] constexpr const T& value() const noexcept { return value_; }

    // Explicit raw extraction for re-wrapping paths.
    [[nodiscard]] constexpr T into() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }

    // Equality / ordering on the underlying value.  Refined values of
    // the same Pred and T compare by their inner T.
    friend constexpr bool operator==(const Refined& a, const Refined& b)
        noexcept(noexcept(a.value_ == b.value_))
    {
        return a.value_ == b.value_;
    }

    friend constexpr auto operator<=>(const Refined& a, const Refined& b)
        noexcept(noexcept(a.value_ <=> b.value_))
        requires std::three_way_comparable<T>
    {
        return a.value_ <=> b.value_;
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

// ── Common refinement aliases ──────────────────────────────────────
//
// Named so they participate in grep/review and don't drift to
// anonymous Refined<positive, T> at every call site (per code_guide
// §XVI: "Every load-bearing predicate gets a named alias").

template <typename T> using NonNull       = Refined<non_null, T>;
template <typename T> using Positive      = Refined<positive, T>;
template <typename T> using NonNegative   = Refined<non_negative, T>;
template <typename T> using PowerOfTwo    = Refined<power_of_two, T>;

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

} // namespace crucible::safety
