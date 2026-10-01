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
#include <foundation/reflect/EnumName.h>

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

namespace detail {

// The carrier of a classified value.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using SecretGraded = Graded<ModalityKind::Comonad, conf::SecretTier, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
