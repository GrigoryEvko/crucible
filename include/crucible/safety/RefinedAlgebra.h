#pragma once

// ── crucible::safety::refined_algebra — predicate composition ────────
//
// Combinators (AllOf / AnyOf / Negate / Implies) over the Refined
// predicate vocabulary already shipped in safety/Refined.h, plus a
// short list of additional unit predicates (exact_size / bounded_below
// / divisible_by) and the most common single-shot type aliases
// (AlignedTo / Sized / Bounded / Capped / Floored / MinSize /
// CacheLineAligned / HugePageAligned).
//
//   Axiom coverage: TypeSafe — composed predicates participate in
//                   Refined<...> exactly like atomic predicates; the
//                   wrapper's invariant chain is preserved.
//                   InitSafe — every combinator is constexpr and
//                   evaluates at construction (semantic=enforce) or
//                   is elided (semantic=ignore).  No runtime state.
//   Runtime cost:   zero on hot path; one branch at boundaries.
//                   sizeof(Refined<all_of<P...>, T>) == sizeof(T) —
//                   verified by static_assert below.
//
// ── Why combinators ─────────────────────────────────────────────────
//
// Refined.h ships ~10 atomic predicates (positive / non_zero /
// non_null / power_of_two / non_empty / aligned<N> / in_range<L,H> /
// bounded_above<M> / length_ge<N>) and a `predicate_implies` trait
// for cross-predicate subsumption.  What was missing: a way to
// *compose* them at the call site without inventing a new struct
// template per combination.  Today a "positive AND ≤ 1024" refinement
// is either two nested Refined wrappers (`Refined<bounded_above<1024>,
// Refined<positive, int>>`) or a hand-rolled struct.  Both forms break
// `predicate_implies` chains and balloon at every call site.
//
// `all_of<positive, bounded_above<1024>>` is a single predicate — one
// type, EBO-collapsed inside Refined, freely combinable with any other
// composed predicate.
//
// ── Composition examples ────────────────────────────────────────────
//
//   using PositiveCapped    = Refined<all_of<positive, bounded_above<1024>>, int>;
//   using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, std::byte*>;
//   using ExactCacheLine    = Refined<exact_size<64>, std::span<std::byte>>;
//   using NonZeroIfPositive = Refined<implies<positive, non_zero>, int>;
//
// Type aliases give one name per shape:
//
//   AlignedTo<64, void*>            = Refined<aligned<64>, void*>
//   Sized<32, std::array<int, 32>>  = Refined<exact_size<32>, std::array<int, 32>>
//   Bounded<0, 100, int>            = Refined<in_range<0, 100>, int>
//   Capped<255, std::uint32_t>      = Refined<bounded_above<255>, std::uint32_t>
//   Floored<1, int>                 = Refined<bounded_below<1>, int>
//   MinSize<8, std::span<int>>      = Refined<length_ge<8>, std::span<int>>
//   DivisibleByN<4, std::size_t>    = Refined<divisible_by<4>, std::size_t>
//   CacheLineAligned<int>           = AlignedTo<64, int*>
//   HugePageAligned<std::byte>      = AlignedTo<2 MiB, std::byte*>
//
// ── Usage at production sites ────────────────────────────────────────
//
//   void* alloc(effects::Alloc, Positive<size_t> n, PowerOfTwo<size_t> a);
//
// can be sharpened to
//
//   void* alloc(effects::Alloc,
//               Refined<all_of<positive, bounded_above<MAX_BLOCK>>, size_t> n,
//               PowerOfTwo<size_t> a);
//
// catching out-of-budget allocations at the construction site, propagating
// the bound to the body via [[assume]] post-pre.
//
// ── Composition with predicate_implies ──────────────────────────────
//
// User code that wants `all_of<P, Q>` to satisfy `is_subsort` of a
// looser refinement (e.g., `all_of<positive, bounded_above<100>>`
// strengthening `positive` alone) should specialize `predicate_implies`
// on the AllOf<...> shape — same recipe as Refined.h's existing
// implications.  See the `predicate_implies` family in Refined.h.

#include <crucible/Platform.h>
#include <crucible/algebra/GradedTrait.h>      // GradedWrapper concept verification
#include <crucible/safety/Refined.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::safety {

// ── Combinators (refined_algebra::*) ────────────────────────────────
//
// Each combinator is a stateless empty class template with a constexpr
// operator() that folds the embedded predicate calls.  Because the
// combinators are themselves predicates, they nest freely:
//
//   all_of<all_of<positive, bounded_above<100>>, divisible_by<4>>
//
// is well-formed and has the obvious meaning.

namespace refined_algebra {

// AllOf<P1, P2, ...>(v) = ∀i. Pᵢ(v) — empty pack is vacuously true.
template <auto... Preds>
struct AllOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0) return true;
        else return (... && Preds(v));
    }
};

template <auto... Preds>
inline constexpr AllOf<Preds...> all_of{};

// AnyOf<P1, P2, ...>(v) = ∃i. Pᵢ(v) — empty pack is vacuously false.
template <auto... Preds>
struct AnyOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0) return false;
        else return (... || Preds(v));
    }
};

template <auto... Preds>
inline constexpr AnyOf<Preds...> any_of{};

// Negate<P>(v) = ¬P(v).  Use sparingly — most invariants are stated
// positively and double negation is a code smell.
template <auto Pred>
struct Negate {
    constexpr bool operator()(auto const& v) const noexcept {
        return !Pred(v);
    }
};

template <auto Pred>
inline constexpr Negate<Pred> negate{};

// Implies<Pre, Post>(v) = Pre(v) ⇒ Post(v) = ¬Pre(v) ∨ Post(v).
// Useful for conditional invariants ("if the value is positive then
// it must also be non-zero" — vacuously true for zero/negative inputs).
template <auto Pre, auto Post>
struct Implies {
    constexpr bool operator()(auto const& v) const noexcept {
        return !Pre(v) || Post(v);
    }
};

template <auto Pre, auto Post>
inline constexpr Implies<Pre, Post> implies{};

}  // namespace refined_algebra

// ── Re-export combinators at safety:: ───────────────────────────────
//
// Callers say `safety::all_of<...>` rather than reaching into the
// algebra sub-namespace.  Refined.h's existing `aligned` / `in_range`
// / `bounded_above` / `length_ge` already live at safety::, so the
// composed form is uniformly safety::* across the algebra.
using refined_algebra::AllOf;
using refined_algebra::AnyOf;
using refined_algebra::Negate;
using refined_algebra::Implies;
using refined_algebra::all_of;
using refined_algebra::any_of;
using refined_algebra::negate;
using refined_algebra::implies;

// ── Additional unit predicates ──────────────────────────────────────

// ExactSize<N>: container of exactly N elements.  Companion to
// length_ge<N>.  An exact-size span is the right type for fixed-shape
// SIMD lane targets (e.g. exact_size<8> for AVX-512 64-bit lanes).
template <std::size_t N>
struct ExactSize {
    constexpr bool operator()(auto const& c) const noexcept {
        return c.size() == N;
    }
};

template <std::size_t N>
inline constexpr ExactSize<N> exact_size{};

// BoundedBelow<Min>: x ≥ Min.  Symmetric to BoundedAbove<Max>.
template <auto Min>
struct BoundedBelow {
    constexpr bool operator()(auto x) const noexcept {
        return x >= decltype(x)(Min);
    }
};

template <auto Min>
inline constexpr BoundedBelow<Min> bounded_below{};

// DivisibleBy<N>: x % N == 0.  Useful for SIMD trip-count gates and
// alignment-of-count refinements (separate from byte-alignment, which
// is `aligned<N>`).  N == 0 is undefined (modulo by zero) — caught
// at the type level so `divisible_by<0>` fires at instantiation
// rather than producing UB at runtime.
template <auto Divisor>
struct DivisibleBy {
    static_assert(Divisor != decltype(Divisor){0},
        "DivisibleBy<0> is undefined (modulo by zero).  Pick a non-"
        "zero divisor or omit the predicate.");
    constexpr bool operator()(auto x) const noexcept {
        return (x % decltype(x)(Divisor)) == decltype(x){0};
    }
};

template <auto Divisor>
inline constexpr DivisibleBy<Divisor> divisible_by{};

// ── Type aliases — common single-shot compositions ──────────────────
//
// Each alias names a shape that previously required spelling out
// `Refined<predicate<args>, T>` at every site.  Aliases participate in
// grep / review and prevent drift between equivalent spellings.

// AlignedTo<N, T>: T aligned to N bytes.  T is typically a pointer
// type; `aligned<N>` from Refined.h takes any pointer.
template <std::size_t N, class T>
using AlignedTo = Refined<aligned<N>, T>;

// Sized<N, S>: container S with exactly N elements.
template <std::size_t N, class S>
using Sized = Refined<exact_size<N>, S>;

// Bounded<Lo, Hi, T>: T with value in the closed range [Lo, Hi].
// Lo > Hi is rejected: the range would be empty (no value satisfies),
// which is always a programming error.  The struct trampoline below
// fires at alias instantiation rather than failing the contract for
// every constructed value.
namespace detail {
    template <auto Lo, auto Hi, class T>
    struct bounded_alias {
        static_assert(Lo <= Hi,
            "Bounded<Lo, Hi, T>: range is empty (Lo > Hi).  No value "
            "of T can satisfy this refinement.  Swap the arguments "
            "or use Capped<Hi, T> / Floored<Lo, T> for single-sided "
            "bounds.");
        using type = Refined<in_range<Lo, Hi>, T>;
    };
}

template <auto Lo, auto Hi, class T>
using Bounded = typename detail::bounded_alias<Lo, Hi, T>::type;

// Capped<Max, T>: T with value ≤ Max.  Single-sided alias of in_range.
template <auto Max, class T>
using Capped = Refined<bounded_above<Max>, T>;

// Floored<Min, T>: T with value ≥ Min.  Single-sided dual of Capped.
template <auto Min, class T>
using Floored = Refined<bounded_below<Min>, T>;

// MinSize<N, S>: container S with at least N elements.
template <std::size_t N, class S>
using MinSize = Refined<length_ge<N>, S>;

// DivisibleByN<N, T>: T whose value is divisible by N.
template <auto N, class T>
using DivisibleByN = Refined<divisible_by<N>, T>;

// ── Cache-hierarchy alignment aliases ───────────────────────────────
//
// Two of the platform constants from CLAUDE.md §XIV are pervasive
// enough to deserve their own aliases.

template <class T>
using CacheLineAligned = AlignedTo<64, T*>;

template <class T>
using HugePageAligned = AlignedTo<2 * 1024 * 1024, T*>;

// ── predicate_implies for combinators ───────────────────────────────
//
// AllOf<P1, ..., Pn> implies any single Pi.  This is the conjunction
// elimination rule: if every Pi holds (the AND), then in particular
// each Pi holds individually.  Wired through the existing
// predicate_implies trait from Refined.h so a refined value
// satisfying a composed predicate participates in is_subsort like
// its individual conjuncts.
//
// AnyOf is the dual: P implies AnyOf<P, ...> (disjunction
// introduction).  If P holds, then P ∨ Q ∨ ... holds.
//
// Reflexivity (X ⇒ X) is NOT installed here for the same reason
// Refined.h omits it: SessionSubtype.h's std::is_same fall-through
// already handles reflexivity at the is_subsort level, and
// duplicating it here invites drift.

namespace detail {

// True iff the type QType is structurally one of the conjunct types.
// Each Preds<i> is a value (auto NTTP); we strip cv from its decltype
// to compare against QType, mirroring the pattern in Refined.h's
// existing predicate_implies specialisations.
template <class QType, auto... Preds>
inline constexpr bool conjunct_matches =
    ((std::is_same_v<std::remove_cv_t<decltype(Preds)>, QType>) || ...);

// Symmetric dual for AnyOf: true iff PType matches one of the
// disjuncts in the AnyOf.
template <class PType, auto... Preds>
inline constexpr bool disjunct_matches =
    ((std::is_same_v<std::remove_cv_t<decltype(Preds)>, PType>) || ...);

// Transitive elimination — true iff some conjunct Pᵢ implies QType
// via the existing predicate_implies trait.  This lets composed
// predicates participate in the lattice through ATOMIC predicates'
// known implications (positive ⇒ non_negative, etc.).
template <class QType, auto... Preds>
inline constexpr bool any_pred_transitively_implies =
    ((predicate_implies<std::remove_cv_t<decltype(Preds)>, QType>::value) || ...);

}  // namespace detail

// AllOf<P1, ..., Pn> ⇒ Q if either:
//   • Q matches one of the conjuncts directly (conjunction
//     elimination), or
//   • some conjunct Pᵢ already implies Q via predicate_implies
//     (transitive elimination).
//
// Both shapes wired through one specialisation; the requires-clause
// disjunction keeps it a single partial ordering.
template <auto... Preds, class QType>
    requires (detail::conjunct_matches<QType, Preds...>
           || detail::any_pred_transitively_implies<QType, Preds...>)
struct predicate_implies<refined_algebra::AllOf<Preds...>, QType>
    : std::true_type {};

// P ⇒ AnyOf<..., P, ...>.  Disjunction introduction.
template <class PType, auto... Preds>
    requires detail::disjunct_matches<PType, Preds...>
struct predicate_implies<PType, refined_algebra::AnyOf<Preds...>>
    : std::true_type {};

// ── Self-test block ─────────────────────────────────────────────────
//
// Per the algebra/effects runtime-smoke-test discipline (memory rule
// `feedback_algebra_runtime_smoke_test_discipline`): pure
// static_assert blocks mask consteval/SFINAE/inline-body bugs.  This
// header ships its inline `runtime_smoke_test()` inside the
// `detail::refined_algebra_self_test` sub-namespace so the sentinel
// TU (test/test_safety_compile.cpp) can call it through the
// established `safety::detail::<name>_self_test::runtime_smoke_test()`
// pathway alongside every other migrated wrapper.
namespace detail::refined_algebra_self_test {

// ── EBO collapse: composed predicates are still zero-byte ────────────
static_assert(sizeof(Refined<all_of<positive, bounded_above<1024>>, int>) == sizeof(int));
static_assert(sizeof(Refined<any_of<positive, non_zero>, int>)            == sizeof(int));
static_assert(sizeof(Refined<negate<positive>, int>)                       == sizeof(int));
static_assert(sizeof(Refined<implies<positive, non_zero>, int>)            == sizeof(int));

// ── AllOf semantics ─────────────────────────────────────────────────
constexpr auto pos_capped = all_of<positive, bounded_above<100>>;
static_assert( pos_capped(50));
static_assert(!pos_capped(0));      // not positive
static_assert(!pos_capped(101));    // exceeds cap

// AllOf with empty pack is vacuously true.
constexpr auto trivially_true = all_of<>;
static_assert(trivially_true(42));
static_assert(trivially_true(-1));
static_assert(trivially_true(0));

// AllOf with a single predicate equals the predicate.
constexpr auto just_positive = all_of<positive>;
static_assert( just_positive(1));
static_assert(!just_positive(0));

// ── AnyOf semantics ─────────────────────────────────────────────────
constexpr auto zero_or_huge = any_of<
    [](int x) constexpr noexcept { return x == 0; },
    [](int x) constexpr noexcept { return x >= 1024; }>;
static_assert( zero_or_huge(0));
static_assert( zero_or_huge(2048));
static_assert(!zero_or_huge(50));

// AnyOf with empty pack is vacuously false.
constexpr auto trivially_false = any_of<>;
static_assert(!trivially_false(42));

// ── Negate semantics ────────────────────────────────────────────────
constexpr auto neg_pos = negate<positive>;
static_assert( neg_pos(0));      // 0 is not positive → negate true
static_assert( neg_pos(-1));
static_assert(!neg_pos(1));      // 1 is positive → negate false

// Double negation collapses semantically.
constexpr auto neg_neg_pos = negate<negate<positive>>;
static_assert( neg_neg_pos(1));
static_assert(!neg_neg_pos(0));

// ── Implies semantics ───────────────────────────────────────────────
constexpr auto pos_implies_nonzero = implies<positive, non_zero>;
static_assert( pos_implies_nonzero(5));   // pos AND nonzero
static_assert( pos_implies_nonzero(0));   // not pos → vacuously true
static_assert( pos_implies_nonzero(-1));  // not pos → vacuously true

// Implies with always-false antecedent is always true.
constexpr auto false_implies_anything = implies<negate<positive>, positive>;
static_assert( false_implies_anything(1));
static_assert(!false_implies_anything(0));   // antecedent true (not positive), consequent false

// ── Additional unit predicates ──────────────────────────────────────
constexpr std::array<int, 8> a8{};
constexpr std::array<int, 7> a7{};
static_assert( exact_size<8>(a8));
static_assert(!exact_size<7>(a8));
static_assert( exact_size<7>(a7));

static_assert( bounded_below<10>(15));
static_assert( bounded_below<10>(10));
static_assert(!bounded_below<10>(9));

static_assert( divisible_by<4>(0));
static_assert( divisible_by<4>(16));
static_assert(!divisible_by<4>(13));
static_assert( divisible_by<8>(2'097'152));

// ── Type alias collapse ─────────────────────────────────────────────
static_assert(sizeof(AlignedTo<64, void*>)            == sizeof(void*));
static_assert(sizeof(Sized<8, std::array<int, 8>>)    == sizeof(std::array<int, 8>));
static_assert(sizeof(Bounded<0, 100, int>)            == sizeof(int));
static_assert(sizeof(Capped<255, std::uint32_t>)      == sizeof(std::uint32_t));
static_assert(sizeof(Floored<1, int>)                 == sizeof(int));
static_assert(sizeof(MinSize<8, std::array<int, 16>>) == sizeof(std::array<int, 16>));
static_assert(sizeof(DivisibleByN<4, std::size_t>)    == sizeof(std::size_t));
static_assert(sizeof(CacheLineAligned<int>)           == sizeof(int*));
static_assert(sizeof(HugePageAligned<std::byte>)      == sizeof(std::byte*));

// ── Type aliases preserve the expected predicate identity ────────────
static_assert(std::is_same_v<typename AlignedTo<64, void*>::predicate_type,
                              std::remove_cv_t<decltype(aligned<64>)>>);
static_assert(std::is_same_v<typename Bounded<0, 100, int>::predicate_type,
                              std::remove_cv_t<decltype(in_range<0, 100>)>>);
static_assert(std::is_same_v<typename Capped<255, std::uint32_t>::predicate_type,
                              std::remove_cv_t<decltype(bounded_above<255>)>>);

// ── Composed-predicate construction at the type level ───────────────
//
// The Trusted{} ctor bypasses the predicate check, so we can verify
// that the composed-predicate type is *constructible* without the test
// itself depending on a predicate value lookup at compile time.
using PositiveCapped    = Refined<all_of<positive, bounded_above<100>>, int>;
using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, void*>;

[[maybe_unused]] constexpr auto _pc1 = PositiveCapped{42, PositiveCapped::Trusted{}};
[[maybe_unused]] constexpr auto _pc2 = PositiveCapped{1,  PositiveCapped::Trusted{}};

// ── predicate_implies: AllOf / AnyOf wiring ─────────────────────────

// AllOf elimination — every conjunct is implied.
static_assert(implies_v<all_of<positive, bounded_above<100>>, positive>);
static_assert(implies_v<all_of<positive, bounded_above<100>>, bounded_above<100>>);
static_assert(implies_v<all_of<non_null, aligned<64>>, non_null>);
static_assert(implies_v<all_of<non_null, aligned<64>>, aligned<64>>);

// AnyOf introduction — each disjunct implies the disjunction.
static_assert(implies_v<positive, any_of<positive, non_zero>>);
static_assert(implies_v<non_zero, any_of<positive, non_zero>>);

// Non-implication: AllOf does NOT imply a predicate that is not one
// of its conjuncts.
static_assert(!implies_v<all_of<positive, bounded_above<100>>, non_null>);

// Transitive elimination — AllOf<positive, ...> ⇒ non_negative
// because positive ⇒ non_negative is in the existing
// predicate_implies lattice.
static_assert(implies_v<all_of<positive, bounded_above<100>>, non_negative>);
// Same for non_zero (positive ⇒ non_zero).
static_assert(implies_v<all_of<positive>, non_zero>);
// Power-of-two ⇒ non_zero, transitively through AllOf.
static_assert(implies_v<all_of<power_of_two, bounded_above<1024u>>, non_zero>);
// AnyOf does NOT imply individual disjuncts (the disjunction does
// not entail any single branch).
static_assert(!implies_v<any_of<positive, non_zero>, positive>);

// ── DivisibleBy / Bounded type-level guards ────────────────────────
//
// Positive coverage: legitimate divisors compile cleanly, bounds in
// proper order produce a usable Bounded type.  Negative coverage
// lives in test/safety_neg/ — instantiating divisible_by<0> or
// Bounded<10, 5, int> fires at compile time.
static_assert(divisible_by<1>(0));
static_assert(divisible_by<8>(64));

[[maybe_unused]] constexpr auto _b_normal = Bounded<0, 100, int>{50};
[[maybe_unused]] constexpr auto _b_equal  = Bounded<5,   5, int>{5};

// ── GradedWrapper concept verification ─────────────────────────────
//
// Refined<combinator-predicate, T> must remain a valid GradedWrapper
// — the combinators don't break the substrate's lattice / value /
// modality contract.  If a future revision of AllOf / AnyOf / Negate
// / Implies disturbs the BoolLattice<Pred> substrate or the
// Absolute modality, these static_asserts fire.
//
// The single-predicate (atomic) refinements already satisfy
// GradedWrapper by way of Refined.h's own MIGRATE-2 work; here we
// pin the COMPOSED forms.

namespace alg = ::crucible::algebra;

static_assert(alg::GradedWrapper<Refined<all_of<positive>, int>>,
    "Refined<all_of<...>, T> must satisfy GradedWrapper");
static_assert(alg::GradedWrapper<Refined<all_of<positive, bounded_above<100>>, int>>);
static_assert(alg::GradedWrapper<Refined<any_of<positive, non_zero>, int>>);
static_assert(alg::GradedWrapper<Refined<negate<positive>, int>>);
static_assert(alg::GradedWrapper<Refined<implies<positive, non_zero>, int>>);
static_assert(alg::GradedWrapper<Refined<all_of<all_of<positive>, bounded_above<1024>>, int>>,
    "nested combinators (AllOf<AllOf<...>, ...>) must also satisfy");

// And the type aliases shipped above:
static_assert(alg::GradedWrapper<AlignedTo<64, void*>>);
static_assert(alg::GradedWrapper<Bounded<0, 100, int>>);
static_assert(alg::GradedWrapper<Capped<255, std::uint32_t>>);
static_assert(alg::GradedWrapper<Floored<1, int>>);
static_assert(alg::GradedWrapper<DivisibleByN<4, std::size_t>>);

// ── Runtime smoke test (per the algebra discipline) ─────────────────
//
// Drives the combinators with non-constant arguments and against a
// move-only payload type to catch consteval/SFINAE/inline-body bugs
// that pure static_asserts would mask.

[[gnu::cold]] inline void runtime_smoke_test() noexcept {
    // ── Non-constant-argument predicate evaluation ──────────────────
    int volatile vol = 42;             // defeat constant-folding
    int x = vol;
    constexpr auto p = all_of<positive, bounded_above<100>>;
    bool ok = p(x);
    static_cast<void>(ok);

    // ── Construct a Refined value through a composed predicate ───────
    Refined<all_of<positive, bounded_above<100>>, int> r{x};
    static_cast<void>(r);

    // ── Move-only payload witness ────────────────────────────────────
    //
    // Refined<P, MoveOnlyT> must not require T to be copyable; the
    // wrapper is move-constructible from its argument.  This exercise
    // catches accidental copy demands introduced by combinators.  We
    // use the Trusted ctor so the test does not depend on the
    // predicate being invocable on MoveOnly — it asserts only that
    // the Refined<P, MoveOnly> TYPE composes (move-constructibility,
    // forwarders, EBO collapse).
    struct MoveOnly {
        int v_ = 0;
        constexpr MoveOnly() noexcept = default;
        constexpr explicit MoveOnly(int v) noexcept : v_{v} {}
        MoveOnly(const MoveOnly&)            = delete;
        MoveOnly(MoveOnly&&)            noexcept = default;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
    };
    static_assert(!std::is_copy_constructible_v<MoveOnly>);
    static_assert( std::is_move_constructible_v<MoveOnly>);

    using RmoT = Refined<positive, MoveOnly>;
    MoveOnly mo{vol};
    RmoT rmo{std::move(mo), RmoT::Trusted{}};
    static_assert(sizeof(RmoT) == sizeof(MoveOnly),
        "Refined<P, MoveOnly> must EBO-collapse to sizeof(MoveOnly) "
        "regardless of T's copyability");
    static_cast<void>(rmo);

    // ── AlignedTo / Sized / Bounded / Capped at runtime ──────────────
    alignas(64) int buf[16] = {};
    AlignedTo<64, int*> ap{buf};
    static_cast<void>(ap);

    std::array<int, 8> arr8_runtime{};
    Sized<8, std::array<int, 8>> sized{arr8_runtime};
    static_cast<void>(sized);

    Bounded<0, 100, int> bd{vol};
    static_cast<void>(bd);

    Capped<255, std::uint32_t> cap{static_cast<std::uint32_t>(vol)};
    static_cast<void>(cap);

    Floored<1, int> fl{vol};
    static_cast<void>(fl);

    // ── DivisibleByN at runtime ─────────────────────────────────────
    std::size_t volatile big = 1024;
    DivisibleByN<4, std::size_t> dN{big};
    static_cast<void>(dN);

    // ── Composed-predicate construction on a real pointer ───────────
    //
    // Combines a non-null test with a 64-byte alignment test against
    // a real cache-line-aligned buffer.  The combined predicate must
    // accept a pointer argument — both `non_null` and `aligned<64>`
    // do, so AllOf<...> does too.
    Refined<all_of<non_null, aligned<64>>, void*> aligned_nonnull_ptr{
        static_cast<void*>(buf)};
    static_cast<void>(aligned_nonnull_ptr);
}

}  // namespace detail::refined_algebra_self_test

}  // namespace crucible::safety
