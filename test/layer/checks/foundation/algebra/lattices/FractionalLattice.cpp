// The compile-time checks of foundation/algebra/lattices/FractionalLattice.h.

#include <foundation/algebra/lattices/FractionalLattice.h>

#include <foundation/core/Number.h>

namespace foundation::algebra::lattices {

namespace detail::fractional_lattice_self_test {

// add is partial on the unit interval, so the shares are no semiring.  The
// concept Semiring evaluates add(one, one), which the precondition of add
// refuses, so this file does not name it.  The shares are a cancellative
// partial commutative monoid: the claim is below.
static_assert(Lattice<FractionalLattice>);
static_assert(BoundedLattice<FractionalLattice>);
static_assert(SemiringShape<FractionalLattice>);
static_assert(PartialCommutativeMonoid<FractionalLattice>);

static_assert(!std::is_empty_v<Rational>);
static_assert(sizeof(Rational) == 16);
static_assert(alignof(Rational) == 8);

static_assert(Rational{1, 2} == Rational{2, 4});
static_assert(Rational{1, 4} < Rational{1, 2});
static_assert(Rational{0, 1} == FractionalLattice::bottom());
static_assert(Rational{1, 1} == FractionalLattice::top());

static_assert(simplify(Rational{2, 4}) == Rational{1, 2});
static_assert(simplify(Rational{6, 8}) == Rational{3, 4});
static_assert(simplify(Rational{0, 5}) == Rational{0, 1});
static_assert(simplify(Rational{5, 5}) == Rational{1, 1});

static_assert(Rational{}.is_well_formed());
static_assert(Rational{1, 2}.is_well_formed());
static_assert(Rational{0, 1}.is_well_formed());
static_assert(Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE}.is_well_formed());
static_assert(!Rational{1, -2}.is_well_formed());
static_assert(!Rational{-1, 2}.is_well_formed());
static_assert(!Rational{1, 0}.is_well_formed());

// A share above one is outside the unit interval.
static_assert(!Rational{3, 2}.is_well_formed());
static_assert(!Rational{2, 1}.is_well_formed());
static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE, 1}.is_well_formed());
static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE - 1}.is_well_formed());

static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE + 1, 1}.is_well_formed());
static_assert(!Rational{1, Rational::MAX_SAFE_MAGNITUDE + 1}.is_well_formed());
static_assert(!Rational{std::int64_t{1} << 40, 1}.is_well_formed());
static_assert(!Rational{1, std::int64_t{1} << 40}.is_well_formed());
static_assert(!Rational{::foundation::core::max_of<std::int64_t>, 1}.is_well_formed());

// Both operands here are outside the bound, so no operation would accept them,
// but the comparison itself must still answer correctly.  A narrow
// cross-product of these two wraps to zero and inverts the answer.
static_assert(Rational{1, std::int64_t{1} << 25} < Rational{std::int64_t{1} << 40, 1});
static_assert(!(Rational{std::int64_t{1} << 40, 1} < Rational{1, std::int64_t{1} << 25}));

// The carrier is infinite, so the axioms are witnessed at a span of shares
// rather than exhausted.
constexpr Rational r0 = FractionalLattice::bottom();
constexpr Rational r14 = Rational{1, 4};
constexpr Rational r12 = Rational{1, 2};
constexpr Rational r34 = Rational{3, 4};
constexpr Rational r1 = FractionalLattice::top();

static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r0, r0));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r12, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r12, r34, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r1, r1, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r34, r12, r14));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r14, r1));

// The claim of the partial commutative monoid of shares, at each triple of
// the witnesses: 0 is the unit of add, and add is commutative, associative
// and cancellative where each sum is defined.  A sum above one has no
// value, so a law that names one does not apply.  The denominators are
// small, so no sum passes the bound.
static_assert(verify_partial_commutative_monoid_over<FractionalLattice>(r0, r14, Rational{1, 3}, r12, Rational{2, 3},
                                                                        r34, r1));
static_assert(verify_cancellative_over<FractionalLattice>(r0, r14, Rational{1, 3}, r12, Rational{2, 3}, r34, r1));

// The laws of mul beside the partial add, at each pair and each triple of
// the witnesses: 1 is the unit of mul, 0 annihilates under mul, mul is
// commutative and associative, and mul distributes over a defined sum.
[[nodiscard]] consteval bool mul_laws_hold_at(Rational a, Rational b, Rational c) {
    using FL = FractionalLattice;
    const bool are_units = FL::mul(a, FL::one()) == a && FL::mul(FL::one(), a) == a
                        && FL::mul(a, FL::zero()) == FL::zero() && FL::mul(FL::zero(), a) == FL::zero();
    const bool is_mul_lawful = FL::mul(a, b) == FL::mul(b, a) && FL::mul(FL::mul(a, b), c) == FL::mul(a, FL::mul(b, c));
    const bool is_distributive =
        !FL::is_sum_defined(a, b) || FL::mul(c, FL::add(a, b)) == FL::add(FL::mul(c, a), FL::mul(c, b));
    return are_units && is_mul_lawful && is_distributive;
}

template <typename... Shares>
[[nodiscard]] consteval bool mul_laws_hold_for_pair(Rational a, Rational b, Shares... shares) {
    return (mul_laws_hold_at(a, b, shares) && ...);
}

template <typename... Shares>
[[nodiscard]] consteval bool mul_laws_hold_for_first(Rational a, Shares... shares) {
    return (mul_laws_hold_for_pair(a, shares, shares...) && ...);
}

// Each triple of the shares.  Complexity: cubic in the shares.
template <typename... Shares>
[[nodiscard]] consteval bool mul_laws_hold(Shares... shares) {
    return (mul_laws_hold_for_first(shares, shares...) && ...);
}

static_assert(mul_laws_hold(r0, r14, Rational{1, 3}, r12, Rational{2, 3}, r34, r1));

static_assert(FractionalLattice::add(r12, r12) == r1,
              "Two halves combine to a full share (split readers → write upgrade).");
static_assert(FractionalLattice::add(r14, r14) == r12, "Two quarters combine to a half (partial reader merge).");
static_assert(FractionalLattice::mul(r12, r12) == r14, "Half of a half is a quarter (recursive split).");
static_assert(FractionalLattice::mul(r1, r12) == r12, "Multiplicative identity: 1 × x = x.");
static_assert(FractionalLattice::mul(r0, r12) == r0, "Multiplicative absorption: 0 × x = 0.");
static_assert(FractionalLattice::leq(r14, r12), "1/4 ⊑ 1/2 in the chain order on shares.");
static_assert(!FractionalLattice::leq(r12, r14), "1/2 ⋢ 1/4 (more share is greater, not less).");
static_assert(FractionalLattice::join(r14, r12) == r12, "Join is max — joining readers picks the biggest share.");
static_assert(FractionalLattice::meet(r14, r12) == r14, "Meet is min — meeting picks the smallest share.");

// The assertions below sit at the top of the bound, where the unreduced
// numerator and denominator reach two to the sixty-second.  They are
// compile-time, so a reduction ordered the other way would fail to compile.
constexpr std::int64_t kHalfBound = Rational::MAX_SAFE_MAGNITUDE / 2;
static_assert(FractionalLattice::add(Rational{kHalfBound, Rational::MAX_SAFE_MAGNITUDE},
                                     Rational{kHalfBound, Rational::MAX_SAFE_MAGNITUDE})
                  == FractionalLattice::top(),
              "Two halves written at the top of the bound must reduce to one over one.");
static_assert(FractionalLattice::add(Rational{1, Rational::MAX_SAFE_MAGNITUDE},
                                     Rational{Rational::MAX_SAFE_MAGNITUDE - 1, Rational::MAX_SAFE_MAGNITUDE})
                  == FractionalLattice::top(),
              "The two parts of a full share split at the bound must reduce to one over one.");
static_assert(FractionalLattice::mul(Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE},
                                     Rational{1, Rational::MAX_SAFE_MAGNITUDE})
                  == Rational{1, Rational::MAX_SAFE_MAGNITUDE},
              "A full share written at the bound must be the unit of mul.");

// The domain of add is a sum of at most one.
static_assert(FractionalLattice::is_sum_defined(r12, r12));
static_assert(FractionalLattice::is_sum_defined(r0, r1));
static_assert(!FractionalLattice::is_sum_defined(r12, r34));
static_assert(!FractionalLattice::is_sum_defined(r1, Rational{1, Rational::MAX_SAFE_MAGNITUDE}));

static_assert(FractionalLattice::name() == "FractionalLattice");

// A larger share is the stronger claim.  A share stored beside a value
// goes through the order dual, where weaken() gives a smaller share.
static_assert(!GradableLattice<FractionalLattice> && GradableLattice<DualLattice<FractionalLattice>>);

// The grade is not empty, so the exact-size layout invariant does not apply
// here and the growth is bounded by hand instead.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

static_assert(sizeof(SharedPermissionGraded<OneByteValue>) == sizeof(OneByteValue) + sizeof(Rational) + 7,
              "A one-byte payload carrying a share must pad out to the share's "
              "alignment.");
static_assert(sizeof(SharedPermissionGraded<EightByteValue>) == sizeof(EightByteValue) + sizeof(Rational),
              "An eight-byte payload carrying a share must need no padding.");

}  // namespace detail::fractional_lattice_self_test

}  // namespace foundation::algebra::lattices
