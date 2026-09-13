#pragma once

// A conjunction of two predicates can already be spelled two other
// ways: as two nested refinement wrappers, or as a hand-rolled struct
// combining them. Both lose. Nesting produces a type the implication
// lattice cannot see through, so every subsumption chain stops at the
// outer wrapper, and both forms grow at each call site. A combinator
// is one predicate type instead, which composes with any other and
// still collapses inside the wrapper.

#include <crucible/Platform.h>
#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Refined.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::safety {

// Each combinator is itself a predicate, so they nest freely.

namespace refined_algebra {

template <auto... Preds>
struct AllOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return true;
        else
            return (... && Preds(v));
    }
};

template <auto... Preds>
inline constexpr AllOf<Preds...> all_of{};

template <auto... Preds>
struct AnyOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return false;
        else
            return (... || Preds(v));
    }
};

template <auto... Preds>
inline constexpr AnyOf<Preds...> any_of{};

template <auto Pred>
struct Negate {
    constexpr bool operator()(auto const& v) const noexcept { return !Pred(v); }
};

template <auto Pred>
inline constexpr Negate<Pred> negate{};

template <auto Pre, auto Post>
struct Implies {
    constexpr bool operator()(auto const& v) const noexcept { return !Pre(v) || Post(v); }
};

template <auto Pre, auto Post>
inline constexpr Implies<Pre, Post> implies{};

}  // namespace refined_algebra

// The atomic predicates already live one namespace out, so the
// combinators are re-exported beside them and the whole vocabulary
// reads the same at a call site.
using refined_algebra::AllOf;
using refined_algebra::AnyOf;
using refined_algebra::Negate;
using refined_algebra::Implies;
using refined_algebra::all_of;
using refined_algebra::any_of;
using refined_algebra::negate;
using refined_algebra::implies;

template <std::size_t N>
struct ExactSize {
    constexpr bool operator()(auto const& c) const noexcept { return c.size() == N; }
};

template <std::size_t N>
inline constexpr ExactSize<N> exact_size{};

template <auto Min>
struct BoundedBelow {
    constexpr bool operator()(auto x) const noexcept { return x >= decltype(x)(Min); }
};

template <auto Min>
inline constexpr BoundedBelow<Min> bounded_below{};

// This is divisibility of a count, distinct from the byte-alignment of
// an address that `aligned` tests.
template <auto Divisor>
struct DivisibleBy {
    static_assert(Divisor != decltype(Divisor){0}, "DivisibleBy<0> is undefined (modulo by zero).  Pick a non-"
                                                   "zero divisor or omit the predicate.");
    constexpr bool operator()(auto x) const noexcept { return (x % decltype(x)(Divisor)) == decltype(x){0}; }
};

template <auto Divisor>
inline constexpr DivisibleBy<Divisor> divisible_by{};

// Naming each shape keeps two spellings of the same refinement from
// drifting apart across call sites.

template <std::size_t N, class T>
using AlignedTo = Refined<aligned<N>, T>;

template <std::size_t N, class S>
using Sized = Refined<exact_size<N>, S>;

// An inverted range is empty and can never be satisfied, so it is
// always a mistake. The trampoline below exists to catch it once at
// alias instantiation instead of at every value's construction.
namespace detail {
template <auto Lo, auto Hi, class T>
struct bounded_alias {
    static_assert(Lo <= Hi, "Bounded<Lo, Hi, T>: range is empty (Lo > Hi).  No value "
                            "of T can satisfy this refinement.  Swap the arguments "
                            "or use Capped<Hi, T> / Floored<Lo, T> for single-sided "
                            "bounds.");
    using type = Refined<in_range<Lo, Hi>, T>;
};
}  // namespace detail

template <auto Lo, auto Hi, class T>
using Bounded = typename detail::bounded_alias<Lo, Hi, T>::type;

template <auto Max, class T>
using Capped = Refined<bounded_above<Max>, T>;

template <auto Min, class T>
using Floored = Refined<bounded_below<Min>, T>;

template <std::size_t N, class S>
using MinSize = Refined<length_ge<N>, S>;

template <auto N, class T>
using DivisibleByN = Refined<divisible_by<N>, T>;

template <class T>
using CacheLineAligned = AlignedTo<64, T*>;

template <class T>
using HugePageAligned = AlignedTo<2 * 1024 * 1024, T*>;

// A conjunction implies each of its conjuncts, and each disjunct
// implies the disjunction. Wiring both through the implication trait
// lets a composed refinement subsume exactly as its parts do.
//
// Reflexivity is deliberately absent here, as it is for the atomic
// predicates: the subsorting machinery already supplies it from a
// same-type fall-through, and stating it twice invites drift.

namespace detail {

// Each predicate arrives as a value, so its type is recovered through
// decltype and stripped of const, the same convention the atomic
// specialisations use.
template <class QType, auto... Preds>
inline constexpr bool conjunct_matches = ((std::is_same_v<std::remove_cv_t<decltype(Preds)>, QType>) || ...);

template <class PType, auto... Preds>
inline constexpr bool disjunct_matches = ((std::is_same_v<std::remove_cv_t<decltype(Preds)>, PType>) || ...);

// This is what lets a composed predicate reach a conclusion through
// what its atomic parts already imply, rather than only through a
// literal match.
template <class QType, auto... Preds>
inline constexpr bool any_pred_transitively_implies =
    ((predicate_implies<std::remove_cv_t<decltype(Preds)>, QType>::value) || ...);

}  // namespace detail

// Both the direct match and the transitive case go through one
// specialisation, so that partial ordering stays unambiguous.
template <auto... Preds, class QType>
    requires(detail::conjunct_matches<QType, Preds...> || detail::any_pred_transitively_implies<QType, Preds...>)
struct predicate_implies<refined_algebra::AllOf<Preds...>, QType> : std::true_type {};

template <class PType, auto... Preds>
    requires detail::disjunct_matches<PType, Preds...>
struct predicate_implies<PType, refined_algebra::AnyOf<Preds...>> : std::true_type {};

// The lower-bound axioms mirror the upper-bound ones but with the
// inequality flipped: a tighter floor is a larger N, whereas a tighter
// ceiling is a smaller one. A range's floor is itself a lower bound.
// The bridges out to the unparameterised predicates are gated: a
// negative floor still admits negative values, and a floor of zero
// still admits zero, so neither reaches non_negative or positive
// respectively. A floor strictly between zero and one is
// conservatively excluded too.

template <auto N, auto M>
    requires(N >= M)
struct predicate_implies<BoundedBelow<N>, BoundedBelow<M>> : std::true_type {};

template <auto L, auto H>
struct predicate_implies<InRange<L, H>, BoundedBelow<L>> : std::true_type {};

template <auto N>
    requires(N >= 0)
struct predicate_implies<BoundedBelow<N>, std::remove_cv_t<decltype(non_negative)>> : std::true_type {};

template <auto N>
    requires(N >= 1)
struct predicate_implies<BoundedBelow<N>, std::remove_cv_t<decltype(positive)>> : std::true_type {};

// The trait does not compose transitively, so this direct bridge is
// needed even though the same conclusion follows by chaining the floor
// bridge to positive with positive implying non-zero. A floor of one
// or more is sound for both categories the predicate accepts: for a
// number the value is at least one, and for a pointer the address is,
// so neither can be the zero value of its type.

template <auto N>
    requires(N >= 1)
struct predicate_implies<BoundedBelow<N>, std::remove_cv_t<decltype(non_zero)>> : std::true_type {};

static_assert(implies_v<bounded_below<10>, bounded_below<5>>,
              "bounded_below<10> ⇒ bounded_below<5>: a tighter floor implies a looser one.");
static_assert(implies_v<bounded_below<5>, bounded_below<5>>, "bounded_below<N> ⇒ bounded_below<N>.");
static_assert(!implies_v<bounded_below<5>, bounded_below<10>>,
              "bounded_below<5> must NOT imply bounded_below<10>: a looser floor "
              "does not imply a tighter one.");
static_assert(implies_v<in_range<5, 100>, bounded_below<5>>,
              "in_range<5, 100> ⇒ bounded_below<5>: a range floor is a lower bound.");
static_assert(implies_v<in_range<0, 200>, bounded_below<0>>, "in_range<0, 200> ⇒ bounded_below<0> at a zero floor.");
static_assert(implies_v<bounded_below<0>, non_negative>,
              "bounded_below<0> ⇒ non_negative: the two predicates coincide at a zero floor.");
static_assert(implies_v<bounded_below<5>, non_negative>, "bounded_below<5> ⇒ non_negative: the floor is positive.");
static_assert(!implies_v<bounded_below<-5>, non_negative>,
              "bounded_below<-5> must NOT imply non_negative: it admits negative "
              "values, which is why the floor clause is load-bearing.");
static_assert(implies_v<bounded_below<1>, positive>, "bounded_below<1> ⇒ positive at the boundary floor.");
static_assert(implies_v<bounded_below<10>, positive>, "bounded_below<10> ⇒ positive at a tighter floor.");
static_assert(!implies_v<bounded_below<0>, positive>, "bounded_below<0> must NOT imply positive: it admits zero, "
                                                      "which is why the floor clause is load-bearing.");

static_assert(implies_v<bounded_below<1>, non_zero>, "bounded_below<1> ⇒ non_zero at the boundary floor.");
static_assert(implies_v<bounded_below<10>, non_zero>, "bounded_below<10> ⇒ non_zero at a tighter floor.");
static_assert(!implies_v<bounded_below<0>, non_zero>, "bounded_below<0> must NOT imply non_zero: it admits zero.");

// The same shape on the size axis. An exact size of N satisfies any
// minimum up to N, and satisfies non-emptiness once N is at least one.
// A size of zero is excluded from the second, since it means the
// container is empty, the very opposite of the conclusion.
//
// The non-emptiness bridge is direct rather than derived, because the
// trait does not compose transitively and the chain through the
// minimum-size axioms would not be reachable.

template <std::size_t N, std::size_t M>
    requires(N >= M)
struct predicate_implies<ExactSize<N>, LengthGe<M>> : std::true_type {};

template <std::size_t N>
    requires(N >= 1)
struct predicate_implies<ExactSize<N>, std::remove_cv_t<decltype(non_empty)>> : std::true_type {};

static_assert(implies_v<exact_size<8>, length_ge<8>>, "exact_size<8> ⇒ length_ge<8>: an exact size satisfies its own "
                                                      "minimum.");
static_assert(implies_v<exact_size<8>, length_ge<4>>, "exact_size<8> ⇒ length_ge<4>: the exact size exceeds a looser "
                                                      "minimum.");
static_assert(implies_v<exact_size<1>, length_ge<0>>, "exact_size<1> ⇒ length_ge<0>: a size is never below zero.");
static_assert(!implies_v<exact_size<4>, length_ge<8>>, "exact_size<4> must NOT imply length_ge<8>: a size of four is "
                                                       "not at least eight.");

static_assert(implies_v<exact_size<1>, non_empty>, "exact_size<1> ⇒ non_empty at the boundary size.");
static_assert(implies_v<exact_size<8>, non_empty>, "exact_size<8> ⇒ non_empty at a larger size.");
static_assert(!implies_v<exact_size<0>, non_empty>, "exact_size<0> must NOT imply non_empty: a size of zero means the "
                                                    "container is empty.");

// The same relation as pointer alignment, on the modulo axis. If x is
// a multiple of N and N is a multiple of M, then x is a multiple of M.
// The clause excluding a zero M matches the predicate's own assertion,
// keeping modulo by zero out before it can be evaluated. The clause
// requiring N at least M already follows from N being a multiple of a
// positive M, and is stated anyway so the requires-clause reads in one
// pass.

template <auto N, auto M>
    requires(N >= M && M > decltype(M){0} && (N % M == decltype(N){0}))
struct predicate_implies<DivisibleBy<N>, DivisibleBy<M>> : std::true_type {};

static_assert(implies_v<divisible_by<4>, divisible_by<4>>, "divisible_by<N> ⇒ divisible_by<N>.");
static_assert(implies_v<divisible_by<8>, divisible_by<4>>,
              "divisible_by<8> ⇒ divisible_by<4>: a multiple of eight is a "
              "multiple of four.");
static_assert(implies_v<divisible_by<16>, divisible_by<4>>,
              "divisible_by<16> ⇒ divisible_by<4>: a wider lane count implies a "
              "narrower one.");
static_assert(implies_v<divisible_by<16>, divisible_by<8>>,
              "divisible_by<16> ⇒ divisible_by<8>: a wider lane count implies a "
              "narrower one.");

static_assert(!implies_v<divisible_by<6>, divisible_by<4>>,
              "divisible_by<6> must NOT imply divisible_by<4>: six is not a "
              "multiple of four, and six itself is a counterexample.");
static_assert(!implies_v<divisible_by<4>, divisible_by<8>>,
              "divisible_by<4> must NOT imply divisible_by<8>: a looser divisor "
              "does not imply a tighter one, and four itself is a "
              "counterexample.");

namespace detail::refined_algebra_self_test {

static_assert(sizeof(Refined<all_of<positive, bounded_above<1024>>, int>) == sizeof(int));
static_assert(sizeof(Refined<any_of<positive, non_zero>, int>) == sizeof(int));
static_assert(sizeof(Refined<negate<positive>, int>) == sizeof(int));
static_assert(sizeof(Refined<implies<positive, non_zero>, int>) == sizeof(int));

constexpr auto pos_capped = all_of<positive, bounded_above<100>>;
static_assert(pos_capped(50));
static_assert(!pos_capped(0));
static_assert(!pos_capped(101));

constexpr auto trivially_true = all_of<>;
static_assert(trivially_true(42));
static_assert(trivially_true(-1));
static_assert(trivially_true(0));

constexpr auto just_positive = all_of<positive>;
static_assert(just_positive(1));
static_assert(!just_positive(0));

constexpr auto zero_or_huge =
    any_of<[](int x) constexpr noexcept { return x == 0; }, [](int x) constexpr noexcept { return x >= 1024; }>;
static_assert(zero_or_huge(0));
static_assert(zero_or_huge(2048));
static_assert(!zero_or_huge(50));

constexpr auto trivially_false = any_of<>;
static_assert(!trivially_false(42));

constexpr auto neg_pos = negate<positive>;
static_assert(neg_pos(0));
static_assert(neg_pos(-1));
static_assert(!neg_pos(1));

constexpr auto neg_neg_pos = negate<negate<positive>>;
static_assert(neg_neg_pos(1));
static_assert(!neg_neg_pos(0));

constexpr auto pos_implies_nonzero = implies<positive, non_zero>;
static_assert(pos_implies_nonzero(5));
static_assert(pos_implies_nonzero(0));
static_assert(pos_implies_nonzero(-1));

// At zero the antecedent holds and the consequent fails, which is the
// one input the implication rejects.
constexpr auto false_implies_anything = implies<negate<positive>, positive>;
static_assert(false_implies_anything(1));
static_assert(!false_implies_anything(0));

constexpr std::array<int, 8> a8{};
constexpr std::array<int, 7> a7{};
static_assert(exact_size<8>(a8));
static_assert(!exact_size<7>(a8));
static_assert(exact_size<7>(a7));

static_assert(bounded_below<10>(15));
static_assert(bounded_below<10>(10));
static_assert(!bounded_below<10>(9));

static_assert(divisible_by<4>(0));
static_assert(divisible_by<4>(16));
static_assert(!divisible_by<4>(13));
static_assert(divisible_by<8>(2097152));

static_assert(sizeof(AlignedTo<64, void*>) == sizeof(void*));
static_assert(sizeof(Sized<8, std::array<int, 8>>) == sizeof(std::array<int, 8>));
static_assert(sizeof(Bounded<0, 100, int>) == sizeof(int));
static_assert(sizeof(Capped<255, std::uint32_t>) == sizeof(std::uint32_t));
static_assert(sizeof(Floored<1, int>) == sizeof(int));
static_assert(sizeof(MinSize<8, std::array<int, 16>>) == sizeof(std::array<int, 16>));
static_assert(sizeof(DivisibleByN<4, std::size_t>) == sizeof(std::size_t));
static_assert(sizeof(CacheLineAligned<int>) == sizeof(int*));
static_assert(sizeof(HugePageAligned<std::byte>) == sizeof(std::byte*));

static_assert(std::is_same_v<typename AlignedTo<64, void*>::predicate_type, std::remove_cv_t<decltype(aligned<64>)>>);
static_assert(
    std::is_same_v<typename Bounded<0, 100, int>::predicate_type, std::remove_cv_t<decltype(in_range<0, 100>)>>);
static_assert(std::is_same_v<typename Capped<255, std::uint32_t>::predicate_type,
                             std::remove_cv_t<decltype(bounded_above<255>)>>);

// Constructing through the Trusted tag keeps this a test of whether
// the composed-predicate type is constructible, without also
// depending on the predicate being evaluated at compile time.
using PositiveCapped = Refined<all_of<positive, bounded_above<100>>, int>;
using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, void*>;

[[maybe_unused]] constexpr auto _pc1 = PositiveCapped{42, PositiveCapped::Trusted{}};
[[maybe_unused]] constexpr auto _pc2 = PositiveCapped{1, PositiveCapped::Trusted{}};

static_assert(implies_v<all_of<positive, bounded_above<100>>, positive>);
static_assert(implies_v<all_of<positive, bounded_above<100>>, bounded_above<100>>);
static_assert(implies_v<all_of<non_null, aligned<64>>, non_null>);
static_assert(implies_v<all_of<non_null, aligned<64>>, aligned<64>>);

static_assert(implies_v<positive, any_of<positive, non_zero>>);
static_assert(implies_v<non_zero, any_of<positive, non_zero>>);

static_assert(!implies_v<all_of<positive, bounded_above<100>>, non_null>);

// The conclusions below hold through a conjunct's own implications,
// not through a literal match against a conjunct.
static_assert(implies_v<all_of<positive, bounded_above<100>>, non_negative>);
static_assert(implies_v<all_of<positive>, non_zero>);
static_assert(implies_v<all_of<power_of_two, bounded_above<1024u>>, non_zero>);
// A disjunction entails none of its branches.
static_assert(!implies_v<any_of<positive, non_zero>, positive>);

static_assert(divisible_by<1>(0));
static_assert(divisible_by<8>(64));

[[maybe_unused]] constexpr auto _b_normal = Bounded<0, 100, int>{50};
[[maybe_unused]] constexpr auto _b_equal = Bounded<5, 5, int>{5};

// The atomic refinements already satisfy the wrapper concept. These
// pin the composed forms, so that a future revision of a combinator
// that disturbs the substrate's lattice, value or modality contract
// fires here.

namespace alg = ::crucible::algebra;

static_assert(alg::GradedWrapper<Refined<all_of<positive>, int>>, "Refined<all_of<...>, T> must satisfy GradedWrapper");
static_assert(alg::GradedWrapper<Refined<all_of<positive, bounded_above<100>>, int>>);
static_assert(alg::GradedWrapper<Refined<any_of<positive, non_zero>, int>>);
static_assert(alg::GradedWrapper<Refined<negate<positive>, int>>);
static_assert(alg::GradedWrapper<Refined<implies<positive, non_zero>, int>>);
static_assert(alg::GradedWrapper<Refined<all_of<all_of<positive>, bounded_above<1024>>, int>>,
              "nested combinators (AllOf<AllOf<...>, ...>) must also satisfy");

static_assert(alg::GradedWrapper<AlignedTo<64, void*>>);
static_assert(alg::GradedWrapper<Bounded<0, 100, int>>);
static_assert(alg::GradedWrapper<Capped<255, std::uint32_t>>);
static_assert(alg::GradedWrapper<Floored<1, int>>);
static_assert(alg::GradedWrapper<DivisibleByN<4, std::size_t>>);

// Driving the combinators with non-constant arguments and a move-only
// payload catches the consteval, substitution and inline-body bugs
// that a block of compile-time assertions alone would mask.

[[gnu::cold]] inline void runtime_smoke_test() noexcept {
    int volatile vol = 42;  // defeats constant folding
    int x = vol;
    constexpr auto p = all_of<positive, bounded_above<100>>;
    bool ok = p(x);
    static_cast<void>(ok);

    Refined<all_of<positive, bounded_above<100>>, int> r{x};
    static_cast<void>(r);

    // The wrapper must not demand a copyable payload, which a
    // combinator could reintroduce by accident. Constructing through
    // the Trusted tag keeps this about type composition rather than
    // about the predicate being callable on a move-only value.
    struct MoveOnly {
        int v_ = 0;
        constexpr MoveOnly() noexcept = default;
        constexpr explicit MoveOnly(int v) noexcept : v_{v} {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
    };
    static_assert(!std::is_copy_constructible_v<MoveOnly>);
    static_assert(std::is_move_constructible_v<MoveOnly>);

    using RmoT = Refined<positive, MoveOnly>;
    MoveOnly mo{vol};
    RmoT rmo{std::move(mo), RmoT::Trusted{}};
    static_assert(sizeof(RmoT) == sizeof(MoveOnly), "Refined<P, MoveOnly> must EBO-collapse to sizeof(MoveOnly) "
                                                    "regardless of T's copyability");
    static_cast<void>(rmo);

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

    std::size_t volatile big = 1024;
    DivisibleByN<4, std::size_t> dN{big};
    static_cast<void>(dN);

    // The composed predicate accepts a pointer argument because both
    // of its conjuncts do.
    Refined<all_of<non_null, aligned<64>>, void*> aligned_nonnull_ptr{static_cast<void*>(buf)};
    static_cast<void>(aligned_nonnull_ptr);
}

}  // namespace detail::refined_algebra_self_test

}  // namespace crucible::safety
