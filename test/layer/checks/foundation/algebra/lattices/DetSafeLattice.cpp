// The compile-time checks of foundation/algebra/lattices/DetSafeLattice.h.

#include <foundation/algebra/lattices/DetSafeLattice.h>

namespace foundation::algebra::lattices {

namespace detail::det_safe_lattice_self_test {

static_assert(::foundation::reflect::enum_count<DetSafeTier> == 7,
              "DetSafeTier catalog diverged from {NDS, FsMtime, Entropy, WallClock, MonoClock, Philox, Pure}.  "
              "Confirm intent and update the callers that pin a tier.");

// The generic walk covers the declaration order, the exhaustive axioms,
// the reflected names and the shape of every At<tier>.
static_assert(verify_chain_lattice<DetSafeLattice>(), "DetSafeLattice: the chain order, the pinned grades or the "
                                                      "reflected names diverged from the DetSafeTier enumerator list.");

static_assert(!UnboundedLattice<DetSafeLattice>);
static_assert(!Semiring<DetSafeLattice>);

// The chain is not a stored grade, and a pinned tier is.  The element of
// a pinned tier is empty, and it names one claim.
static_assert(!GradableLattice<DetSafeLattice>);
static_assert(claim_orientation_v<det_safe_tier::PureTier> == ClaimOrientation::one_claim);

// The specific pins: which enumerators bound the chain, and the exact
// spellings that the reflection builds.
static_assert(DetSafeLattice::bottom() == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::top() == DetSafeTier::Pure);

static_assert(DetSafeLattice::name() == "DetSafeLattice");
static_assert(det_safe_tier::NdsTier::name() == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(det_safe_tier::PureTier::name() == "DetSafeLattice::At<Pure>");
static_assert(DetSafeLattice::At<static_cast<DetSafeTier>(255)>::name() == "DetSafeLattice::At<?>");

static_assert(det_safe_tier::NdsTier::tier == DetSafeTier::NonDeterministicSyscall);
static_assert(det_safe_tier::PureTier::tier == DetSafeTier::Pure);

}  // namespace detail::det_safe_lattice_self_test

}  // namespace foundation::algebra::lattices
