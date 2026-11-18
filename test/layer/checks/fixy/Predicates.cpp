// The compile-time checks of fixy/Predicates.h.

#include <fixy/Predicates.h>

namespace fixy {

// The checks of the implication relation between the predicates are in the
// check file of fixy/PredicateImplies.h.

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

// Two values with a size, as exact_size reads them.
template <std::size_t Count>
struct SizedProbe {
    [[nodiscard]] constexpr std::size_t size() const noexcept { return Count; }
};
constexpr SizedProbe<8> a8{};
constexpr SizedProbe<7> a7{};
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

static_assert(divisible_by<1>(0));
static_assert(divisible_by<8>(64));

}  // namespace detail::refined_algebra_self_test

}  // namespace fixy
