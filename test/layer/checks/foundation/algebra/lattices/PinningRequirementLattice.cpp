// The compile-time checks of foundation/algebra/lattices/PinningRequirementLattice.h.

#include <foundation/algebra/lattices/PinningRequirementLattice.h>

namespace foundation::algebra::lattices {

namespace detail::pinning_requirement_lattice_self_test {

static_assert(::foundation::reflect::enum_count<PinningRequirement> == 4,
              "PinningRequirement catalog diverged from {NotRequired, PerCore, PerSocket, CrossSocketSafe}.  A new "
              "level needs every composite that names a level rechecked.");

static_assert(verify_chain_lattice<PinningRequirementLattice>(),
              "PinningRequirementLattice: the chain order, the pinned grades or "
              "the reflected names diverged from the PinningRequirement "
              "enumerator list.");

static_assert(!UnboundedLattice<PinningRequirementLattice>);
static_assert(!Semiring<PinningRequirementLattice>);

static_assert(PinningRequirementLattice::bottom() == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::top() == PinningRequirement::CrossSocketSafe);

static_assert(PinningRequirementLattice::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket),
              "A socket-coherent source serves a per-core consumer, because socket "
              "coherence contains core coherence.");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::PerSocket, PinningRequirement::PerCore),
              "A merely core-coherent source does not serve a consumer that migrates "
              "across the socket.  That pairing is the backwards-delta read this "
              "axis forbids.");

static_assert(PinningRequirementLattice::name() == "PinningRequirementLattice");
static_assert(pinning_requirement::NotRequiredPin::name() == "PinningRequirementLattice::At<NotRequired>");
static_assert(pinning_requirement::CrossSocketSafePin::name() == "PinningRequirementLattice::At<CrossSocketSafe>");
static_assert(PinningRequirementLattice::At<static_cast<PinningRequirement>(255)>::name()
              == "PinningRequirementLattice::At<?>");

static_assert(pinning_requirement::NotRequiredPin::requirement == PinningRequirement::NotRequired);
static_assert(pinning_requirement::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

}  // namespace detail::pinning_requirement_lattice_self_test

}  // namespace foundation::algebra::lattices
