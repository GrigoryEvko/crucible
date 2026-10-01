// The compile-time checks of foundation/algebra/lattices/TrustLattice.h.

#include <foundation/algebra/lattices/TrustLattice.h>

namespace foundation::algebra::lattices {

namespace detail::trust_lattice_self_test {

// The witnesses span four independent tag namespaces plus a templated
// source, because the lattice must work for any Source whatsoever.  The
// source tags are in the detail namespace of the header.
namespace trust {
struct Verified {};
struct Tested {};
struct Unverified {};
}  // namespace trust
namespace access {
struct RW {};
struct RO {};
struct WO {};
}  // namespace access
namespace version {
struct V1 {};
struct V2 {};
template <int N>
struct V {};  // templated source witness
}  // namespace version

static_assert(Lattice<TrustLattice<source::FromUser>>);
static_assert(Lattice<TrustLattice<source::FromDb>>);
static_assert(Lattice<TrustLattice<source::Sanitized>>);
static_assert(Lattice<TrustLattice<trust::Verified>>);
static_assert(Lattice<TrustLattice<trust::Unverified>>);
static_assert(Lattice<TrustLattice<access::RW>>);
static_assert(Lattice<TrustLattice<access::WO>>);
static_assert(Lattice<TrustLattice<version::V1>>);
static_assert(Lattice<TrustLattice<version::V<2>>>);

static_assert(BoundedLattice<TrustLattice<source::FromUser>>);
static_assert(BoundedLattice<TrustLattice<trust::Verified>>);
static_assert(BoundedLattice<TrustLattice<access::RW>>);
static_assert(BoundedLattice<TrustLattice<version::V<7>>>);

// Emptiness is the precondition for the grade to collapse under EBO, and
// it makes the order one claim, which Graded stores with no orientation.
static_assert(claim_orientation_v<TrustLattice<source::FromUser>> == ClaimOrientation::one_claim);
static_assert(GradableLattice<TrustLattice<source::FromUser>>);
static_assert(std::is_empty_v<TrustLattice<source::FromUser>::element_type>);
static_assert(std::is_empty_v<TrustLattice<trust::Verified>::element_type>);
static_assert(std::is_empty_v<TrustLattice<access::RW>::element_type>);
static_assert(std::is_empty_v<TrustLattice<version::V<2>>::element_type>);

static_assert(verify_bounded_lattice_axioms_at<TrustLattice<source::FromUser>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<trust::Verified>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<access::RW>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<version::V1>>({}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<version::V<3>>>({}, {}, {}));

static_assert(std::is_same_v<TrustLattice<source::FromUser>::source_type, source::FromUser>);
static_assert(std::is_same_v<TrustLattice<source::FromUser>::element_type::source_type, source::FromUser>);
static_assert(std::is_same_v<TrustLattice<version::V<5>>::source_type, version::V<5>>);

// Composition takes two values over the same lattice.  These
// distinctness assertions are what make that signature enough to stop a
// value from one source combining with a value from another.
static_assert(!std::is_same_v<TrustLattice<source::FromUser>, TrustLattice<source::FromDb>>);
static_assert(!std::is_same_v<TrustLattice<source::External>, TrustLattice<source::Sanitized>>);
static_assert(!std::is_same_v<TrustLattice<trust::Verified>, TrustLattice<trust::Unverified>>);
static_assert(!std::is_same_v<TrustLattice<source::FromUser>, TrustLattice<trust::Verified>>);
static_assert(!std::is_same_v<TrustLattice<version::V<1>>, TrustLattice<version::V<2>>>);

// display_string_of returns the simple name or the qualified one
// depending on the scope chain of the translation unit doing the
// including.  Match with ends_with, never with ==, or these assertions
// hold in one translation unit and fail in the next.
static_assert(TrustLattice<source::FromUser>::name().ends_with("FromUser"));
static_assert(TrustLattice<source::Sanitized>::name().ends_with("Sanitized"));
static_assert(TrustLattice<trust::Verified>::name().ends_with("Verified"));
static_assert(TrustLattice<access::RW>::name().ends_with("RW"));
static_assert(TrustLattice<version::V1>::name().ends_with("V1"));
// A templated source keeps its argument in the display string.
static_assert(TrustLattice<version::V<7>>::name().ends_with("V<7>"));

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using TaggedFromUser = Graded<ModalityKind::RelativeMonad, TrustLattice<source::FromUser>, T>;
template <typename T>
using TaggedVerified = Graded<ModalityKind::RelativeMonad, TrustLattice<trust::Verified>, T>;
template <typename T>
using TaggedV2 = Graded<ModalityKind::RelativeMonad, TrustLattice<version::V<2>>, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedSanitized, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedVerified, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedV2, EightByteValue);
// The arithmetic witnesses pin the collapse across the
// trivially-default-constructible split as well as the class one.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedSanitized, double);

}  // namespace detail::trust_lattice_self_test

}  // namespace foundation::algebra::lattices
