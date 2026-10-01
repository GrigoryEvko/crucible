// The compile-time checks of foundation/algebra/lattices/ConfLattice.h.

#include <foundation/algebra/lattices/ConfLattice.h>

namespace foundation::algebra::lattices {

namespace detail::conf_lattice_self_test {

static_assert(::foundation::reflect::enum_count<Conf> == 2, "Conf must hold exactly the two levels Public and Secret.");

static_assert(verify_chain_lattice<ConfLattice>(), "ConfLattice: the chain order, the pinned grades or the reflected "
                                                   "names diverged from the Conf enumerator list.");

static_assert(ConfLattice::leq(Conf::Public, Conf::Secret), "Public ⊑ Secret in the confidentiality chain.");
static_assert(!ConfLattice::leq(Conf::Secret, Conf::Public),
              "Secret ⋢ Public — declassification doesn't go via lattice "
              "weakening, only via the named declassify counit.");
static_assert(ConfLattice::join(Conf::Public, Conf::Secret) == Conf::Secret,
              "Joining mixed classifications raises to the higher one.");
static_assert(ConfLattice::meet(Conf::Public, Conf::Secret) == Conf::Public,
              "Meeting mixed classifications lowers to the lower one.");

static_assert(ConfLattice::name() == "ConfLattice");
static_assert(ConfLattice::At<Conf::Public>::name() == "ConfLattice::At<Public>");
static_assert(ConfLattice::At<Conf::Secret>::name() == "ConfLattice::At<Secret>");
static_assert(ConfLattice::At<static_cast<Conf>(9)>::name() == "ConfLattice::At<?>");

static_assert(conf::PublicTier::classification == Conf::Public);
static_assert(conf::SecretTier::classification == Conf::Secret);
static_assert(ConfLattice::bottom() == Conf::Public && ConfLattice::top() == Conf::Secret);
static_assert(claim_orientation_v<ConfLattice> == ClaimOrientation::weaker_is_higher);

// The payloads of a classified value.  The runtime smoke test in
// test/foundation/test_lattices_core.cpp builds one.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, EightByteValue);

}  // namespace detail::conf_lattice_self_test

}  // namespace foundation::algebra::lattices
