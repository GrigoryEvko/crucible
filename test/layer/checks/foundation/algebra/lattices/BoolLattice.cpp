// The compile-time checks of foundation/algebra/lattices/BoolLattice.h.

#include <foundation/algebra/lattices/BoolLattice.h>

namespace foundation::algebra::lattices {

namespace detail::bool_lattice_self_test {

static_assert(Lattice<BoolLattice<positive>>);
static_assert(BoundedLattice<BoolLattice<positive>>);
static_assert(Lattice<BoolLattice<non_negative>>);
static_assert(Lattice<BoolLattice<non_zero>>);

// Emptiness is the precondition for the grade to collapse under EBO, and
// it makes the order one claim, which Graded stores with no orientation.
static_assert(claim_orientation_v<BoolLattice<positive>> == ClaimOrientation::one_claim);
static_assert(GradableLattice<BoolLattice<positive>>);
static_assert(std::is_empty_v<BoolLattice<positive>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_negative>::element_type>);
static_assert(std::is_empty_v<BoolLattice<non_zero>::element_type>);

static_assert(verify_bounded_lattice_axioms_at<BoolLattice<positive>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_negative>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<BoolLattice<non_zero>>({}, {}, {}));

// display_string_of returns the simple name or the fully qualified one
// depending on the scope chain of the translation unit doing the
// including.  Match with ends_with, never with ==, or these assertions
// hold in one translation unit and fail in the next.
static_assert(BoolLattice<positive>::name().ends_with("positive"));
static_assert(BoolLattice<non_negative>::name().ends_with("non_negative"));
static_assert(BoolLattice<non_zero>::name().ends_with("non_zero"));

static_assert(std::is_same_v<BoolLattice<positive>::predicate_type, positive>);
static_assert(std::is_same_v<BoolLattice<positive>::element_type::predicate_type, positive>);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, EightByteValue);
// The arithmetic witnesses pin the collapse across the
// trivially-default-constructible split as well as the class one.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedPositive, double);

}  // namespace detail::bool_lattice_self_test

}  // namespace foundation::algebra::lattices
