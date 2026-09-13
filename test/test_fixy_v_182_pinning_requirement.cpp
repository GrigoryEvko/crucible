#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/PinningRequirementLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace pr = ::crucible::algebra::lattices::pinning_requirement;

namespace {

using cal::PinningRequirement;
using L = cal::PinningRequirementLattice;

static_assert(crucible::algebra::Lattice<L>, "PinningRequirementLattice must satisfy the Lattice concept "
                                             "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
              "PinningRequirementLattice has both bottom() (NotRequired) and top() "
              "(CrossSocketSafe) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "PinningRequirementLattice is not a semiring — it carries no "
                                               "equality+add+mul algebra, only the order-theoretic operations.");

static_assert(cal::pinning_requirement_count == 4,
              "PinningRequirement must have exactly 4 enumerators {NotRequired, "
              "PerCore, PerSocket, CrossSocketSafe}.  A new one must take the "
              "ordinal that keeps integer order equal to coherence-domain breadth, "
              "and must be added to both name switches.");

static_assert(std::is_same_v<std::underlying_type_t<PinningRequirement>, std::uint8_t>,
              "PinningRequirement must use uint8_t as its underlying type — the "
              "ordinal is the coherence-domain rank, from NotRequired at 0 to "
              "CrossSocketSafe at 3.");

static_assert(std::to_underlying(PinningRequirement::NotRequired) == 0);
static_assert(std::to_underlying(PinningRequirement::PerCore) == 1);
static_assert(std::to_underlying(PinningRequirement::PerSocket) == 2);
static_assert(std::to_underlying(PinningRequirement::CrossSocketSafe) == 3);

static_assert(L::bottom() == PinningRequirement::NotRequired);
static_assert(L::top() == PinningRequirement::CrossSocketSafe);

static_assert(L::leq(PinningRequirement::NotRequired, PinningRequirement::PerCore));
static_assert(L::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket));
static_assert(L::leq(PinningRequirement::PerSocket, PinningRequirement::CrossSocketSafe));
static_assert(L::leq(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe), "transitive endpoints");

// The chain must not run downwards.  A source coherent only per core
// must never satisfy a per-socket or wider requirement.  That mismatch
// is the cross-socket negative-delta bug in a timestamp read, refused
// here at the type level.
static_assert(!L::leq(PinningRequirement::PerSocket, PinningRequirement::PerCore),
              "PerCore ⋣ PerSocket — a per-core-coherent source does not satisfy "
              "a socket-wide requirement.");
static_assert(!L::leq(PinningRequirement::CrossSocketSafe, PinningRequirement::PerSocket));
static_assert(!L::leq(PinningRequirement::PerCore, PinningRequirement::NotRequired));

// Join takes the wider coherence domain, meet the narrower.
static_assert(L::join(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
              == PinningRequirement::CrossSocketSafe);
static_assert(L::join(PinningRequirement::PerCore, PinningRequirement::PerSocket) == PinningRequirement::PerSocket);
static_assert(L::join(PinningRequirement::NotRequired, PinningRequirement::PerCore) == PinningRequirement::PerCore,
              "NotRequired is the join identity");
static_assert(L::join(PinningRequirement::CrossSocketSafe, PinningRequirement::PerCore)
                  == PinningRequirement::CrossSocketSafe,
              "CrossSocketSafe absorbs in join");

static_assert(L::meet(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
                  == PinningRequirement::NotRequired,
              "NotRequired absorbs in meet");
static_assert(L::meet(PinningRequirement::PerCore, PinningRequirement::PerSocket) == PinningRequirement::PerCore);
static_assert(L::meet(PinningRequirement::CrossSocketSafe, PinningRequirement::CrossSocketSafe)
                  == PinningRequirement::CrossSocketSafe,
              "CrossSocketSafe is the meet identity at the top");

static_assert(crucible::algebra::Lattice<pr::NotRequiredPin>);
static_assert(crucible::algebra::Lattice<pr::PerCorePin>);
static_assert(crucible::algebra::BoundedLattice<pr::CrossSocketSafePin>);
static_assert(std::is_empty_v<pr::NotRequiredPin::element_type>,
              "At<NotRequired>::element_type must be empty so Graded<Absolute, "
              "At<NotRequired>, P> collapses to sizeof(P) — a zero-byte "
              "pinning-requirement annotation at every binding site.");
static_assert(std::is_empty_v<pr::PerCorePin::element_type>);
static_assert(std::is_empty_v<pr::PerSocketPin::element_type>);
static_assert(std::is_empty_v<pr::CrossSocketSafePin::element_type>);
static_assert(pr::PerCorePin::requirement == PinningRequirement::PerCore,
              "At<P>::requirement must equal P at the type level, so a wrapper can "
              "read the pinned requirement with no runtime data.");
static_assert(pr::PerSocketPin::requirement == PinningRequirement::PerSocket);
static_assert(pr::NotRequiredPin::requirement == PinningRequirement::NotRequired);
static_assert(pr::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, pr::PerCorePin, EightByteValue>)
        == sizeof(EightByteValue),
    "Pinning a PerCore grade must add zero bytes to an 8-byte payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, pr::NotRequiredPin, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"PinningRequirementLattice"});
static_assert(pr::PerCorePin::name() == std::string_view{"PinningRequirementLattice::At<PerCore>"});
static_assert(pr::PerSocketPin::name() == std::string_view{"PinningRequirementLattice::At<PerSocket>"});
static_assert(pr::CrossSocketSafePin::name() == std::string_view{"PinningRequirementLattice::At<CrossSocketSafe>"});
static_assert(pr::NotRequiredPin::name() == std::string_view{"PinningRequirementLattice::At<NotRequired>"});
static_assert(cal::pinning_requirement_name(PinningRequirement::PerSocket) == std::string_view{"PerSocket"});

}  // namespace

int main() {
    cal::detail::pinning_requirement_lattice_self_test::runtime_smoke_test();
    return 0;
}
