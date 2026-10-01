#pragma once

// Three-tier chain over the durability scope of a piece of attached
// state.
//
// The longest scope sits at the top, so `leq(PER_REQUEST, PER_FLEET)`
// holds: a fleet-scoped value lives through every window a
// request-scoped consumer could observe it in, so the fleet provider
// satisfies the request consumer.
//
// Do not read that direction as the comonadic one.  Narrowing a
// fleet-scoped value to a program-scoped view runs opposite to ⊑ and is
// a separate operation on the wrapper, not a use of this order.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class Lifetime : std::uint8_t {
    PER_REQUEST = 0,  // destroyed on session close
    PER_PROGRAM = 1,  // shared across sibling sessions of the same program
    PER_FLEET = 2,  // consensus-replicated across the whole fleet
};

// A longer scope is the stronger claim.
struct LifetimeLattice : EnumChainLattice<LifetimeLattice, Lifetime, ClaimOrientation::stronger_is_higher> {
    template <Lifetime L>
    struct At : PinnedAt<LifetimeLattice, L> {
        static constexpr Lifetime scope = L;
    };
};

namespace lifetime {
using PerRequestTier = LifetimeLattice::At<Lifetime::PER_REQUEST>;
using PerProgramTier = LifetimeLattice::At<Lifetime::PER_PROGRAM>;
using PerFleetTier = LifetimeLattice::At<Lifetime::PER_FLEET>;
}  // namespace lifetime

}  // namespace foundation::algebra::lattices
