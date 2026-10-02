// The compile-time checks of fixy/Refined.h.

#include <fixy/Refined.h>

namespace fixy {

namespace detail::admit_refined_self_test {

enum class Refusal : std::uint8_t {
    NotPositive
};

static_assert(admit_refined<positive>(3, Refusal::NotPositive).value().value() == 3);
static_assert(admit_refined<positive>(0, Refusal::NotPositive).error() == Refusal::NotPositive);
static_assert(admit_refined<positive>(-1, Refusal::NotPositive).error() == Refusal::NotPositive);

}  // namespace detail::admit_refined_self_test

// No byte route builds a refined value, and the constructors stay trivial
// so that a refined value still passes in a register.
static_assert(!std::is_trivially_copyable_v<Refined<positive, int>>
                  && !std::is_trivially_copyable_v<SealedRefined<positive, int>>,
              "std::bit_cast must not build a refined value from bytes that the predicate never saw");
static_assert(std::is_trivially_copy_constructible_v<Refined<positive, int>>
                  && std::is_trivially_move_constructible_v<Refined<positive, int>>
                  && std::is_trivially_destructible_v<Refined<positive, int>>
                  && sizeof(Refined<positive, int>) == sizeof(int),
              "a refined int keeps the layout and the register passing of an int");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Refined<positive, int>>,
              "the checked lifetime start refuses a refined value over bytes");

// Each axiom of fixy/Refined.h is witnessed here, positively and at the
// boundary where its soundness clause bites.  Several conclusions below
// hold only through a chain, and they witness the closure.  A refusal
// cannot be enumerated from the namespace, which is why these stay
// written by hand.

static_assert(PredicateImplies<non_null, non_zero>, "non_null ⇒ non_zero: a non-null pointer is a non-zero pointer.");
static_assert(PredicateImplies<non_zero, non_null>,
              "non_zero ⇒ non_null: the implication is bidirectional for a pointer.");
static_assert(PredicateImplies<positive, non_null>,
              "positive ⇒ non_zero ⇒ non_null: a chain can end on a narrowing edge.");
static_assert(!PredicateImplies<non_null, positive>, "non_null reaches non_zero and nothing past it.");
static_assert(!PredicateImplies<non_zero, non_zero>,
              "the closure adds no reflexive answer through the non_null cycle.");

static_assert(PredicateImplies<in_range<5, 9>, bounded_above<20>>,
              "in_range<5, 9> ⇒ bounded_above<9> ⇒ bounded_above<20>: the ceiling family chains into its weakening.");
static_assert(!PredicateImplies<bounded_above<20>, in_range<5, 9>>, "a chain runs one way: a ceiling gives no range.");
static_assert(!PredicateImplies<in_range<5, 9>, bounded_above<8>>, "the chain keeps the ceiling: nine is above eight.");
static_assert(PredicateImplies<in_range<5, 9>, bounded_below<1>>,
              "in_range<5, 9> ⇒ bounded_below<5> ⇒ bounded_below<1>: the floor family chains too.");
static_assert(PredicateImplies<all_of<in_range<5, 9>, non_negative>, bounded_above<20>>,
              "a conjunct reaches its conclusion through the closure.");
static_assert(PredicateImplies<length_ge<1>, non_empty>,
              "length_ge<1> ⇒ non_empty: a size of at least one is not empty.");
static_assert(PredicateImplies<length_ge<8>, non_empty>, "length_ge<N> ⇒ non_empty for every N of at least one.");
static_assert(!PredicateImplies<length_ge<0>, non_empty>,
              "length_ge<0> is vacuous and must NOT imply non_empty: an empty "
              "container satisfies it.");

static_assert(PredicateImplies<length_ge<8>, length_ge<1>>,
              "length_ge<8> ⇒ length_ge<1>: the longer minimum is stronger.");

static_assert(PredicateImplies<in_range<0, 100>, non_negative>,
              "in_range<0, 100> ⇒ non_negative: the lower bound is zero.");
static_assert(PredicateImplies<in_range<5, 100>, non_negative>,
              "in_range<5, 100> ⇒ non_negative: the lower bound is positive.");
static_assert(PredicateImplies<in_range<0u, 255u>, non_negative>, "An unsigned bound carries non_negative trivially.");
static_assert(!PredicateImplies<in_range<-5, 100>, non_negative>,
              "in_range<-5, 100> admits negative values and must NOT imply "
              "non_negative: the lower-bound clause is load-bearing.");

static_assert(PredicateImplies<in_range<5, 100>, in_range<0, 200>>,
              "in_range<5, 100> ⇒ in_range<0, 200>: a tighter range implies a looser one.");

static_assert(PredicateImplies<in_range<1, 100>, positive>, "in_range<1, 100> ⇒ positive at the boundary lower bound.");
static_assert(PredicateImplies<in_range<5, 100>, positive>, "in_range<5, 100> ⇒ positive at an interior lower bound.");
static_assert(!PredicateImplies<in_range<0, 100>, positive>,
              "in_range<0, 100> must NOT imply positive: it admits zero, "
              "which is why the lower-bound clause is load-bearing.");
static_assert(!PredicateImplies<in_range<-5, 100>, positive>,
              "in_range<-5, 100> must NOT imply positive: it admits negative "
              "values.");

static_assert(PredicateImplies<in_range<1, 100>, non_zero>, "in_range<1, 100> ⇒ non_zero at the boundary lower bound.");
static_assert(PredicateImplies<in_range<5, 100>, non_zero>, "in_range<5, 100> ⇒ non_zero at an interior lower bound.");

static_assert(PredicateImplies<in_range<-100, -1>, non_zero>,
              "in_range<-100, -1> ⇒ non_zero at the boundary upper bound.");
static_assert(PredicateImplies<in_range<-100, -5>, non_zero>,
              "in_range<-100, -5> ⇒ non_zero at an interior upper bound.");

static_assert(!PredicateImplies<in_range<0, 100>, non_zero>,
              "in_range<0, 100> must NOT imply non_zero: it admits zero, and "
              "neither branch of the disjunctive clause holds.");
static_assert(!PredicateImplies<in_range<-5, 5>, non_zero>,
              "in_range<-5, 5> must NOT imply non_zero: the range straddles "
              "zero.");
static_assert(!PredicateImplies<in_range<-100, 0>, non_zero>,
              "in_range<-100, 0> must NOT imply non_zero: it admits zero at the "
              "upper bound.");

static_assert(PredicateImplies<bounded_below<10>, bounded_below<5>>,
              "bounded_below<10> ⇒ bounded_below<5>: a tighter floor implies a looser one.");
static_assert(PredicateImplies<bounded_below<5>, bounded_below<5>>, "bounded_below<N> ⇒ bounded_below<N>.");
static_assert(!PredicateImplies<bounded_below<5>, bounded_below<10>>,
              "bounded_below<5> must NOT imply bounded_below<10>: a looser floor "
              "does not imply a tighter one.");
static_assert(PredicateImplies<in_range<5, 100>, bounded_below<5>>,
              "in_range<5, 100> ⇒ bounded_below<5>: a range floor is a lower bound.");
static_assert(PredicateImplies<in_range<0, 200>, bounded_below<0>>,
              "in_range<0, 200> ⇒ bounded_below<0> at a zero floor.");
static_assert(PredicateImplies<bounded_below<0>, non_negative>,
              "bounded_below<0> ⇒ non_negative: the two predicates coincide at a zero floor.");
static_assert(PredicateImplies<bounded_below<5>, non_negative>,
              "bounded_below<5> ⇒ non_negative: the floor is positive.");
static_assert(!PredicateImplies<bounded_below<-5>, non_negative>,
              "bounded_below<-5> must NOT imply non_negative: it admits negative "
              "values, which is why the floor clause is load-bearing.");
static_assert(PredicateImplies<bounded_below<1>, positive>, "bounded_below<1> ⇒ positive at the boundary floor.");
static_assert(PredicateImplies<bounded_below<10>, positive>, "bounded_below<10> ⇒ positive at a tighter floor.");
static_assert(!PredicateImplies<bounded_below<0>, positive>,
              "bounded_below<0> must NOT imply positive: it admits zero, "
              "which is why the floor clause is load-bearing.");

static_assert(PredicateImplies<bounded_below<1>, non_zero>, "bounded_below<1> ⇒ non_zero at the boundary floor.");
static_assert(PredicateImplies<bounded_below<10>, non_zero>, "bounded_below<10> ⇒ non_zero at a tighter floor.");
static_assert(!PredicateImplies<bounded_below<0>, non_zero>,
              "bounded_below<0> must NOT imply non_zero: it admits zero.");

static_assert(PredicateImplies<exact_size<8>, length_ge<8>>,
              "exact_size<8> ⇒ length_ge<8>: an exact size satisfies its own "
              "minimum.");
static_assert(PredicateImplies<exact_size<8>, length_ge<4>>,
              "exact_size<8> ⇒ length_ge<4>: the exact size exceeds a looser "
              "minimum.");
static_assert(PredicateImplies<exact_size<1>, length_ge<0>>,
              "exact_size<1> ⇒ length_ge<0>: a size is never below zero.");
static_assert(!PredicateImplies<exact_size<4>, length_ge<8>>,
              "exact_size<4> must NOT imply length_ge<8>: a size of four is "
              "not at least eight.");

static_assert(PredicateImplies<exact_size<1>, non_empty>, "exact_size<1> ⇒ non_empty at the boundary size.");
static_assert(PredicateImplies<exact_size<8>, non_empty>, "exact_size<8> ⇒ non_empty at a larger size.");
static_assert(!PredicateImplies<exact_size<0>, non_empty>,
              "exact_size<0> must NOT imply non_empty: a size of zero means the "
              "container is empty.");

static_assert(PredicateImplies<divisible_by<4>, divisible_by<4>>, "divisible_by<N> ⇒ divisible_by<N>.");
static_assert(PredicateImplies<divisible_by<8>, divisible_by<4>>,
              "divisible_by<8> ⇒ divisible_by<4>: a multiple of eight is a "
              "multiple of four.");
static_assert(PredicateImplies<divisible_by<16>, divisible_by<4>>,
              "divisible_by<16> ⇒ divisible_by<4>: a wider lane count implies a "
              "narrower one.");
static_assert(PredicateImplies<divisible_by<16>, divisible_by<8>>,
              "divisible_by<16> ⇒ divisible_by<8>: a wider lane count implies a "
              "narrower one.");

static_assert(!PredicateImplies<divisible_by<6>, divisible_by<4>>,
              "divisible_by<6> must NOT imply divisible_by<4>: six is not a "
              "multiple of four, and six itself is a counterexample.");
static_assert(PredicateImplies<divisible_by<(refined::widest_unsigned{1} << 90)>,
                               divisible_by<(refined::widest_unsigned{1} << 70)>>
                  && !PredicateImplies<divisible_by<(refined::widest_unsigned{1} << 70)>,
                                       divisible_by<(refined::widest_unsigned{1} << 90)>>,
              "divisible_by compares divisors wider than std::uintmax_t by value, and no divisor narrows to zero.");
static_assert(!PredicateImplies<divisible_by<4>, divisible_by<8>>,
              "divisible_by<4> must NOT imply divisible_by<8>: a looser divisor "
              "does not imply a tighter one, and four itself is a "
              "counterexample.");

namespace detail::refined_edge_walk {

// The number of narrowing edges in the namespace.
[[nodiscard]] consteval std::size_t narrowing_edge_count() noexcept {
    std::size_t count = 0;
    for (const std::meta::info m :
         std::meta::members_of(^^refined::admitted_implications, std::meta::access_context::unchecked())) {
        if (refined::is_narrowing_edge(m)) ++count;
    }
    return count;
}

}  // namespace detail::refined_edge_walk

namespace detail::refined_self_test {

// Each type in exact_integer_types is one that ExactInteger admits.
[[nodiscard]] consteval bool every_bound_type_is_exact() noexcept {
    bool is_exact = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info type : refined::exact_integer_types) {
        is_exact = is_exact && refined::ExactInteger<typename[:type:]>;
    }
#pragma GCC diagnostic pop
    return is_exact;
}
static_assert(every_bound_type_is_exact());

// read_bound gives the sign and the magnitude of an exact integer bound,
// also at the most negative value of the widest type, and no exact
// integer for a bound of another type.
template <auto Bound>
struct bound_holder {};

[[nodiscard]] consteval refined::exact_bound bound_read_of(std::meta::info holder) {
    return refined::read_bound(std::meta::template_arguments_of(holder)[0]);
}

inline constexpr refined::widest_signed most_negative_bound =
    -static_cast<refined::widest_signed>(refined::widest_unsigned{1} << 126) * 2;

static_assert(bound_read_of(^^bound_holder<-1>).is_exact_integer && bound_read_of(^^bound_holder<-1>).is_negative
              && bound_read_of(^^bound_holder<-1>).magnitude == 1);
static_assert(bound_read_of(^^bound_holder<9u>).is_exact_integer && !bound_read_of(^^bound_holder<9u>).is_negative
              && bound_read_of(^^bound_holder<9u>).magnitude == 9);
static_assert(bound_read_of(^^bound_holder<most_negative_bound>).is_negative
              && bound_read_of(^^bound_holder<most_negative_bound>).magnitude == (refined::widest_unsigned{1} << 127));
static_assert(!bound_read_of(^^bound_holder<'a'>).is_exact_integer
              && !bound_read_of(^^bound_holder<true>).is_exact_integer
              && !bound_read_of(^^bound_holder<1.5>).is_exact_integer);
static_assert(!PredicateImplies<bounded_above<1.5>, bounded_above<2.5>>,
              "a bound that is not an exact integer defines its predicate on no value type, so no step reads it");

// fixy/Refined.h holds the walk over the atomic edges and its rosters of
// sample values.
static_assert(refined_edge_walk::every_edge_holds());

// The namespace gives the counts, and a new edge is a two-place edit
// that a reviewer sees.
static_assert(::foundation::fail_closed::edge_count<^^refined::admitted_implications>() == 4,
              "the four edges that keep or widen the domain: positive ⇒ non_negative, positive ⇒ non_zero, "
              "power_of_two ⇒ non_zero, and non_null ⇒ non_zero");

static_assert(refined_edge_walk::narrowing_edge_count() == 1, "the one narrowing edge: non_zero ⇒ non_null");

// The layout witnesses are one fold over a roster of wrapper types
// rather than one line per type.  The roster is the only hand list.
template <class... Ws>
[[nodiscard]] consteval bool all_collapse_to_value() noexcept {
    return ((sizeof(Ws) == sizeof(typename Ws::value_type)) && ...);
}

template <class... Ws>
[[nodiscard]] consteval bool all_graded_wrappers() noexcept {
    return (::foundation::algebra::GradedWrapper<Ws> && ...);
}

// One representative shape per alias: a scalar, a container that has
// both an emptiness test and a size, a parameterised length bound over
// that container, and the parameterised predicates, which are empty
// classes and so collapse inside the wrapper too.
static_assert(all_collapse_to_value<
              Refined<positive, int>, Refined<non_null, void*>, Refined<power_of_two, std::size_t>,
              Refined<aligned<64>, void*>, Refined<bounded_above<1024u>, int>, Refined<in_range<0, 100>, int>,
              Refined<length_ge<1>, void*>, NonZero<int>, NonEmpty<std::span<int>>, NonEmptySpan<int>,
              SealedRefined<positive, int>, SealedRefined<non_null, void*>,
              Refined<all_of<positive, bounded_above<1024>>, int>, Refined<any_of<positive, non_zero>, int>,
              Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>, AlignedTo<64, void*>,
              Sized<8, std::array<int, 8>>, Bounded<0, 100, int>, Capped<255, std::uint32_t>, Floored<1, int>,
              MinSize<8, std::array<int, 16>>, DivisibleByN<4, std::size_t>, CacheLineAligned<int>,
              HugePageAligned<std::byte>>());

// Both wrappers are regime 1, so the composition is the payload.  The
// claim is the untracked build's: with fixy/Qtt.h's consume tracker on,
// Linear carries one byte of state on purpose and this collapse is the
// thing that build gives up.
static_assert(::fixy::qtt_consume_tracked || sizeof(LinearRefined<non_null, void*>) == sizeof(void*),
              "LinearRefined must collapse to sizeof(T)");

// The atomic and the composed refinements alike satisfy the wrapper
// concept, so that a future revision of a combinator that disturbs the
// substrate's lattice, value or modality contract fires here.
static_assert(
    all_graded_wrappers<
        Refined<positive, int>, Refined<non_null, void*>, SealedRefined<positive, int>, Refined<all_of<positive>, int>,
        Refined<all_of<positive, bounded_above<100>>, int>, Refined<any_of<positive, non_zero>, int>,
        Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>,
        Refined<all_of<all_of<positive>, bounded_above<1024>>, int>, AlignedTo<64, void*>, Bounded<0, 100, int>,
        Capped<255, std::uint32_t>, Floored<1, int>, DivisibleByN<4, std::size_t>>(),
    "every Refined and SealedRefined shape must satisfy GradedWrapper, nested combinators included");

// The layout of the bare value, pinned over the arithmetic and pointer
// shapes, with one difference on purpose: a refined value is not
// trivially copyable, so no byte pattern becomes one.  The shared layout
// invariant asserts that parity, so the three other properties are stated
// here one by one, and the fourth inverted.
template <typename Refinedness, typename T>
inline constexpr bool keeps_the_value_layout =
    sizeof(Refinedness) == sizeof(T) && alignof(Refinedness) == alignof(T)
    && std::is_trivially_destructible_v<Refinedness> == std::is_trivially_destructible_v<T>
    && !std::is_trivially_copyable_v<Refinedness>;

template <typename T>
using SealedPositive = SealedRefined<positive, T>;
static_assert(keeps_the_value_layout<Positive<int>, int> && keeps_the_value_layout<Positive<double>, double>
              && keeps_the_value_layout<NonNull<void*>, void*> && keeps_the_value_layout<SealedPositive<int>, int>
              && keeps_the_value_layout<SealedPositive<double>, double>);

// One door: no public constructor takes a bare value.
static_assert(!std::is_constructible_v<Refined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<Refined<positive, int>>);
static_assert(!std::is_constructible_v<SealedRefined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<SealedRefined<positive, int>>);
static_assert(std::is_constructible_v<SealedRefined<positive, int>, Refined<positive, int>&&>);

// The mints run in a constant expression, and the checked one runs the
// predicate there.
static_assert(mint_refined<positive>(42).value() == 42);
static_assert(mint_refined_trusted<positive>(-1).value() == -1);
static_assert(mint_sealed_refined<positive>(42).value() == 42);
static_assert(mint_sealed_refined_trusted<positive>(-1).value() == -1);

static_assert(std::is_same_v<typename AlignedTo<64, void*>::predicate_type, std::remove_cv_t<decltype(aligned<64>)>>);
static_assert(
    std::is_same_v<typename Bounded<0, 100, int>::predicate_type, std::remove_cv_t<decltype(in_range<0, 100>)>>);
static_assert(std::is_same_v<typename Capped<255, std::uint32_t>::predicate_type,
                             std::remove_cv_t<decltype(bounded_above<255>)>>);

// The extraction traits see through cv and reference, reject a
// lookalike with the same member aliases, and tell the two wrappers
// apart.
struct LookalikeRefined {
    using value_type = int;
    using predicate_type = decltype(positive);
};

static_assert(is_refined_v<Refined<positive, int>>);
static_assert(is_refined_v<Refined<non_negative, int>>);
static_assert(is_refined_v<SealedRefined<positive, int>>);
static_assert(is_refined_v<Refined<positive, int>&>);
static_assert(is_refined_v<Refined<positive, int>&&>);
static_assert(is_refined_v<Refined<positive, int> const>);
static_assert(is_refined_v<Refined<positive, int> const&>);
static_assert(is_refined_v<Refined<positive, int> volatile>);
static_assert(!is_refined_v<int>);
static_assert(!is_refined_v<int*>);
static_assert(!is_refined_v<Refined<positive, int>*>);
static_assert(!is_refined_v<void>);
static_assert(!is_refined_v<LookalikeRefined>);
static_assert(IsRefined<Refined<positive, int>>);
static_assert(IsRefined<SealedRefined<positive, int> const&>);
static_assert(!IsRefined<int>);
static_assert(std::is_same_v<refined_value_t<Refined<positive, int>>, int>);
static_assert(std::is_same_v<refined_value_t<SealedRefined<positive, int> const&>, int>);
static_assert(std::is_same_v<refined_predicate_type_t<Refined<positive, int>>, refined::predicate_t<positive>>);
static_assert(!refined_is_sealed_v<Refined<positive, int>>);
static_assert(refined_is_sealed_v<SealedRefined<positive, int>>);

}  // namespace detail::refined_self_test

namespace detail::refined_algebra_self_test {

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

// A bound compares by value, and a bound that the value type does not
// hold leaves the predicate undefined there.
static_assert(!bounded_above<-1>(0) && bounded_above<-1>(-1) && !in_range<0u, 10u>(-1) && in_range<-5, 5u>(5));
static_assert(PredicateInvocableOn<bounded_below<255>, std::uint8_t> && PredicateInvocableOn<in_range<0u, 9u>, int>);
static_assert(!PredicateInvocableOn<bounded_below<256>, std::uint8_t>
              && !PredicateInvocableOn<in_range<256, 511>, std::uint8_t>
              && !PredicateInvocableOn<bounded_above<-1>, unsigned>
              && !PredicateInvocableOn<divisible_by<256>, std::uint8_t> && !PredicateInvocableOn<divisible_by<-4>, int>
              && !PredicateInvocableOn<bounded_above<8>, bool> && !PredicateInvocableOn<bounded_above<8>, char>);
static_assert(PredicateInvocableOn<bounded_above<8>, double> && PredicateInvocableOn<bounded_above<1.0e30>, double>
              && PredicateInvocableOn<in_range<0.0f, 1.0f>, double> && in_range<0.0f, 1.0f>(0.5f)
              && !in_range<0.0f, 1.0f>(-0.5f) && !bounded_above<1.0e30>(2.0e30) && bounded_below<-3>(-2.5));
static_assert(!PredicateInvocableOn<bounded_above<1.0e30>, float> && !PredicateInvocableOn<bounded_above<8.5>, int>
              && !PredicateInvocableOn<divisible_by<4>, double>
              && !PredicateInvocableOn<bounded_above<std::numeric_limits<double>::infinity()>, double>
              && !PredicateInvocableOn<bounded_above<(std::int64_t{1} << 60)>, double>);
static_assert(!PredicateInvocableOn<all_of<positive, bounded_below<256>>, std::uint8_t>
              && !PredicateInvocableOn<negate<bounded_above<-1>>, unsigned>
              && !PredicateInvocableOn<implies<positive, in_range<256, 511>>, std::uint8_t>);

// Minting through the trusted door keeps this a test of whether the
// composed-predicate type is constructible, without also depending on
// the predicate being evaluated at compile time.
using PositiveCapped = Refined<all_of<positive, bounded_above<100>>, int>;
using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, void*>;

[[maybe_unused]] constexpr auto pc1_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(42);
[[maybe_unused]] constexpr auto pc2_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(1);

static_assert(PredicateImplies<all_of<positive, bounded_above<100>>, positive>);
static_assert(PredicateImplies<all_of<positive, bounded_above<100>>, bounded_above<100>>);
static_assert(PredicateImplies<all_of<non_null, aligned<64>>, non_null>);
static_assert(PredicateImplies<all_of<non_null, aligned<64>>, aligned<64>>);

static_assert(PredicateImplies<positive, any_of<positive, non_zero>>);
static_assert(PredicateImplies<non_zero, any_of<positive, non_zero>>);

static_assert(!PredicateImplies<all_of<positive, bounded_above<100>>, non_empty>);
// positive reaches non_null through non_zero, and the chain ends on the
// narrowing edge.  The conjunction reaches non_null too.
static_assert(PredicateImplies<all_of<positive, bounded_above<100>>, non_null>);

// The conclusions below hold through a conjunct's own implications,
// not through a literal match against a conjunct.
static_assert(PredicateImplies<all_of<positive, bounded_above<100>>, non_negative>);
static_assert(PredicateImplies<all_of<positive>, non_zero>);
static_assert(PredicateImplies<all_of<power_of_two, bounded_above<1024u>>, non_zero>);
// A disjunction entails none of its branches.
static_assert(!PredicateImplies<any_of<positive, non_zero>, positive>);

static_assert(divisible_by<1>(0));
static_assert(divisible_by<8>(64));

// The checked mint in a constant expression runs the predicate there,
// and the alias trampoline admits the boundary where Lo equals Hi.
[[maybe_unused]] constexpr Bounded<0, 100, int> b_normal_witness = mint_refined<in_range<0, 100>, int>(50);
[[maybe_unused]] constexpr Bounded<5, 5, int> b_equal_witness = mint_refined<in_range<5, 5>, int>(5);

}  // namespace detail::refined_algebra_self_test

}  // namespace fixy
