// The compile-time checks of fixy/Predicates.h.

#include <fixy/Predicates.h>

namespace fixy {

// Each axiom of fixy/Predicates.h is witnessed here, positively and at the
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

// The two widest integer types are exact integers, so their bounds
// compare by value like the bounds of a standard type.
static_assert(refined::ExactInteger<refined::widest_signed> && refined::ExactInteger<refined::widest_unsigned>);

// bound_at_most compares two bounds by value, also at the most negative
// value of the widest type and across a sign, and it refuses a bound that
// is not an exact integer.  bound_divides compares a divisor that is
// wider than std::uintmax_t, and refuses a divisor less than one.
inline constexpr refined::widest_signed most_negative_bound =
    -static_cast<refined::widest_signed>(refined::widest_unsigned{1} << 126) * 2;

static_assert(refined::bound_at_most(std::meta::reflect_constant(-1), std::meta::reflect_constant(0u))
              && !refined::bound_at_most(std::meta::reflect_constant(9u), std::meta::reflect_constant(-1))
              && refined::bound_at_most(std::meta::reflect_constant(most_negative_bound),
                                        std::meta::reflect_constant(-1))
              && !refined::bound_at_most(std::meta::reflect_constant(-1),
                                         std::meta::reflect_constant(most_negative_bound)));
static_assert(!refined::bound_at_most(std::meta::reflect_constant('a'), std::meta::reflect_constant('b'))
              && !refined::bound_at_most(std::meta::reflect_constant(false), std::meta::reflect_constant(true))
              && !refined::bound_at_most(std::meta::reflect_constant(1.5), std::meta::reflect_constant(2.5)));
static_assert(refined::bound_divides(std::meta::reflect_constant(refined::widest_unsigned{1} << 90),
                                     std::meta::reflect_constant(refined::widest_unsigned{1} << 70))
              && !refined::bound_divides(std::meta::reflect_constant(12), std::meta::reflect_constant(0))
              && !refined::bound_divides(std::meta::reflect_constant(12), std::meta::reflect_constant(-4))
              && !refined::bound_divides(std::meta::reflect_constant(12), std::meta::reflect_constant(24)));
static_assert(!PredicateImplies<bounded_above<1.5>, bounded_above<2.5>>,
              "a bound that is not an exact integer defines its predicate on no value type, so no step reads it");

// fixy/Predicates.h holds the walk over the atomic edges and its rosters
// of sample values.
static_assert(refined_edge_walk::every_edge_holds());

// The namespace gives the counts, and a new edge is a two-place edit
// that a reviewer sees.
static_assert(::foundation::fail_closed::edge_count<^^refined::admitted_implications>() == 4,
              "the four edges that keep or widen the domain: positive ⇒ non_negative, positive ⇒ non_zero, "
              "power_of_two ⇒ non_zero, and non_null ⇒ non_zero");

static_assert(refined_edge_walk::narrowing_edge_count() == 1, "the one narrowing edge: non_zero ⇒ non_null");

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

}  // namespace detail::refined_algebra_self_test

}  // namespace fixy
