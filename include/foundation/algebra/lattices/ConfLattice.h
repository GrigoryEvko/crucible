#pragma once

// Two-element confidentiality chain, Public below Secret.  A join raises
// the classification of mixed operands and a meet lowers it.
//
// Lowering by meet is not declassification.  Information leaves a
// classified wrapper only through the comonadic counit, which every call
// site names, and never by weakening the grade.
//
// A classified value always sits at the top position, because an
// unclassified value is a plain value rather than a wrapped one.  That
// is what lets the fixed-position sub-lattice carry an empty grade and
// collapse to the size of the payload.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class Conf : std::int8_t {
    Public = 0,
    Secret = 1,
};

inline constexpr std::size_t conf_count = ::foundation::reflect::enum_count<Conf>;

// The identifier of c, or "<unknown Conf>" for a value outside the enum.
[[nodiscard]] consteval std::string_view conf_name(Conf c) noexcept { return ::foundation::reflect::enum_name(c); }

// A higher classification promises less about where the value can go,
// and it is the weaker claim.  weaken() moves a stored grade up, which
// increases the classification and is sound.  A move down of a stored
// grade is a declassification, and Graded does not do it.
struct ConfLattice : EnumChainLattice<ConfLattice, Conf, ClaimOrientation::weaker_is_higher> {
    template <Conf C>
    struct At : PinnedAt<ConfLattice, C> {
        static constexpr Conf classification = C;
    };
};

namespace conf {
using PublicTier = ConfLattice::At<Conf::Public>;
using SecretTier = ConfLattice::At<Conf::Secret>;
}  // namespace conf

namespace detail::conf_lattice_self_test {

static_assert(conf_count == 2, "Conf must hold exactly the two levels Public and Secret.");

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
static_assert(conf_name(Conf::Public) == "Public");
static_assert(conf_name(Conf::Secret) == "Secret");
static_assert(conf_name(static_cast<Conf>(9)) == "<unknown Conf>");

static_assert(conf::PublicTier::classification == Conf::Public);
static_assert(conf::SecretTier::classification == Conf::Secret);
static_assert(ConfLattice::bottom() == Conf::Public && ConfLattice::top() == Conf::Secret);
static_assert(claim_orientation_v<ConfLattice> == ClaimOrientation::weaker_is_higher);

// The carrier of a classified value.  The runtime smoke test in
// test/foundation/test_lattices_core.cpp builds one.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using SecretGraded = Graded<ModalityKind::Comonad, conf::SecretTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, EightByteValue);

}  // namespace detail::conf_lattice_self_test

}  // namespace foundation::algebra::lattices
