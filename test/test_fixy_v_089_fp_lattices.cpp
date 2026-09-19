#include <crucible/algebra/lattices/AllLattices.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/_Modality.h>

#include <string_view>
#include <type_traits>

namespace cal = ::crucible::algebra::lattices;
namespace ca = ::crucible::algebra;

namespace {

// A lattice header that is included but never added to the umbrella's
// name-coverage pack ships an unnamed lattice, and nothing complains. These
// assertions reach every one of the eleven through the umbrella alone, so
// dropping an include from it stops this file compiling.
static_assert(::crucible::algebra::HasLatticeName<cal::FpRoundingLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpFtzLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpContractLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpTrapMaskLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpDenormalInputLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpNanPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpInfPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpComplexLayoutLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpLibmPolicyLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpReassociateLattice>);
static_assert(::crucible::algebra::HasLatticeName<cal::FpConstantRoundingLattice>);

// The header already pins these through a macro applied per lattice.
// Repeating them here catches a refactor that drops the macro invocation
// rather than the property.
static_assert(ca::BoundedLattice<cal::FpRoundingLattice>);
static_assert(ca::BoundedLattice<cal::FpFtzLattice>);
static_assert(ca::BoundedLattice<cal::FpContractLattice>);
static_assert(ca::BoundedLattice<cal::FpTrapMaskLattice>);
static_assert(ca::BoundedLattice<cal::FpDenormalInputLattice>);
static_assert(ca::BoundedLattice<cal::FpNanPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpInfPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpComplexLayoutLattice>);
static_assert(ca::BoundedLattice<cal::FpLibmPolicyLattice>);
static_assert(ca::BoundedLattice<cal::FpReassociateLattice>);
static_assert(ca::BoundedLattice<cal::FpConstantRoundingLattice>);

// None of them is a semiring. The carrier is a non-numeric chain, and the
// semiring concept wants a sum and a product distinct from join and meet.
static_assert(!ca::Semiring<cal::FpRoundingLattice>);
static_assert(!ca::Semiring<cal::FpLibmPolicyLattice>);

// The chain axioms at their endpoints: bottom joined with anything is that
// thing, top met with anything is that thing, and leq is reflexive. A
// degenerate reimplementation of any of the three reddens here.
static_assert(cal::FpRoundingLattice::join(cal::FpRoundingLattice::bottom(), cal::FpRounding::RoundToNearestEven)
              == cal::FpRounding::RoundToNearestEven);
static_assert(cal::FpRoundingLattice::meet(cal::FpRoundingLattice::top(), cal::FpRounding::RoundToNearestEven)
              == cal::FpRounding::RoundToNearestEven);
static_assert(cal::FpRoundingLattice::leq(cal::FpRounding::RoundToNearestEven, cal::FpRounding::RoundToNearestEven));

static_assert(cal::FpFtzLattice::join(cal::FpFtzLattice::bottom(), cal::FpFtz::FlushToZero) == cal::FpFtz::FlushToZero);
static_assert(cal::FpFtzLattice::meet(cal::FpFtzLattice::top(), cal::FpFtz::PreserveSubnormals)
              == cal::FpFtz::PreserveSubnormals);

static_assert(cal::FpContractLattice::join(cal::FpContract::Off, cal::FpContract::Fast) == cal::FpContract::Fast);
static_assert(cal::FpTrapMaskLattice::join(cal::FpTrapMask::AllMasked, cal::FpTrapMask::UnmaskedInexact)
              == cal::FpTrapMask::UnmaskedInexact);
static_assert(cal::FpInfPolicyLattice::leq(cal::FpInfPolicy::PropagateInfinity, cal::FpInfPolicy::FlushInfToFinite));

// Distinct types, so joining a value of one sub-axis under another sub-axis's
// lattice is a compile error rather than a silent wrong answer.
static_assert(!std::is_same_v<cal::FpRoundingLattice, cal::FpFtzLattice>);
static_assert(!std::is_same_v<cal::FpContractLattice, cal::FpReassociateLattice>);
static_assert(!std::is_same_v<cal::FpTrapMaskLattice, cal::FpNanPolicyLattice>);
static_assert(!std::is_same_v<cal::FpComplexLayoutLattice, cal::FpLibmPolicyLattice>);
static_assert(!std::is_same_v<cal::FpDenormalInputLattice, cal::FpInfPolicyLattice>);
static_assert(!std::is_same_v<cal::FpRoundingLattice, cal::FpConstantRoundingLattice>);

// A name() that falls back to the unnamed-lattice sentinel, or to an empty
// string, is caught by pinning the exact spelling rather than its length.
static_assert(cal::FpRoundingLattice::name() == std::string_view{"FpRoundingLattice"});
static_assert(cal::FpFtzLattice::name() == std::string_view{"FpFtzLattice"});
static_assert(cal::FpContractLattice::name() == std::string_view{"FpContractLattice"});
static_assert(cal::FpTrapMaskLattice::name() == std::string_view{"FpTrapMaskLattice"});
static_assert(cal::FpDenormalInputLattice::name() == std::string_view{"FpDenormalInputLattice"});
static_assert(cal::FpNanPolicyLattice::name() == std::string_view{"FpNanPolicyLattice"});
static_assert(cal::FpInfPolicyLattice::name() == std::string_view{"FpInfPolicyLattice"});
static_assert(cal::FpComplexLayoutLattice::name() == std::string_view{"FpComplexLayoutLattice"});
static_assert(cal::FpLibmPolicyLattice::name() == std::string_view{"FpLibmPolicyLattice"});
static_assert(cal::FpReassociateLattice::name() == std::string_view{"FpReassociateLattice"});
static_assert(cal::FpConstantRoundingLattice::name() == std::string_view{"FpConstantRoundingLattice"});

// A graded wrapper over one of these lattices collapses to the size of its
// payload only while the singleton sub-lattice's element type is empty. Every
// zero-cost claim made about those wrappers rests on the assertions below.
static_assert(std::is_empty_v<cal::FpRoundingLattice::At<cal::FpRounding::RoundToNearestEven>::element_type>);
static_assert(std::is_empty_v<cal::FpFtzLattice::At<cal::FpFtz::FlushToZero>::element_type>);
static_assert(std::is_empty_v<cal::FpContractLattice::At<cal::FpContract::Fast>::element_type>);
static_assert(std::is_empty_v<cal::FpTrapMaskLattice::At<cal::FpTrapMask::AllMasked>::element_type>);
static_assert(std::is_empty_v<cal::FpReassociateLattice::At<cal::FpReassociate::Forbidden>::element_type>);

}  // namespace

int main() {
    // The lattice operations driven with non-constant arguments on every
    // sub-axis. Nothing above this point leaves constant evaluation.
    cal::detail::fp_mode_lattice_self_test::fp_mode_lattice_runtime_smoke_test();
    return 0;
}
