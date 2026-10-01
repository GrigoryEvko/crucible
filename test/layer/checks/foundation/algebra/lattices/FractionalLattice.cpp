// The compile-time checks of foundation/algebra/lattices/FractionalLattice.h.

#include <foundation/algebra/lattices/FractionalLattice.h>

namespace foundation::algebra::lattices {

namespace detail::fractional_lattice_self_test {

static_assert(Lattice<FractionalLattice>);
static_assert(BoundedLattice<FractionalLattice>);
static_assert(Semiring<FractionalLattice>);

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

static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE + 1, 1}.is_well_formed());
static_assert(!Rational{1, Rational::MAX_SAFE_MAGNITUDE + 1}.is_well_formed());
static_assert(!Rational{std::int64_t{1} << 40, 1}.is_well_formed());
static_assert(!Rational{1, std::int64_t{1} << 40}.is_well_formed());
static_assert(!Rational{std::numeric_limits<std::int64_t>::max(), 1}.is_well_formed());

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

static_assert(verify_semiring_axioms_at<FractionalLattice>(r0, r0, r0));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r14, r14));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r0, r12, r1));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r1, r1, r1));

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

// The two assertions below sit exactly at the top of the bound, where the
// unreduced numerator reaches the value that a narrowing cast would wrap.  They
// are compile-time, so a reduction ordered the other way would not merely give
// a wrong answer here, it would fail to compile.
static_assert(FractionalLattice::add(Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE},
                                     Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE})
                  == Rational{2, 1},
              "Adding two full shares written at the top of the bound must reduce to "
              "two over one, without overflow.");
static_assert(FractionalLattice::add(Rational{Rational::MAX_SAFE_MAGNITUDE, 1},
                                     Rational{Rational::MAX_SAFE_MAGNITUDE, 1})
                  == Rational{std::int64_t{2} * Rational::MAX_SAFE_MAGNITUDE, 1},
              "Adding two shares at the bound over one must double the numerator, "
              "which still fits.");
// The same sum written in the small form must agree with the bounded form.
static_assert(FractionalLattice::add(FractionalLattice::top(), FractionalLattice::top()) == Rational{2, 1});

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
