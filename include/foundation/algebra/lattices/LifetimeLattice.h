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
#include <foundation/reflect/Enumerate.h>

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

inline constexpr std::size_t lifetime_count = ::foundation::reflect::enum_count<Lifetime>;

// The identifier of l, or "<unknown Lifetime>" for a value outside the
// enum.
[[nodiscard]] consteval std::string_view lifetime_name(Lifetime l) noexcept {
    return ::foundation::reflect::enum_name(l);
}

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

namespace detail::lifetime_lattice_self_test {

static_assert(lifetime_count == 3, "Lifetime must hold exactly the three scopes PER_REQUEST, PER_PROGRAM and "
                                   "PER_FLEET.");

static_assert(verify_chain_lattice<LifetimeLattice>(),
              "LifetimeLattice: the chain order, the pinned grades or the reflected "
              "names diverged from the Lifetime enumerator list.");

static_assert(!UnboundedLattice<LifetimeLattice>);
static_assert(!Semiring<LifetimeLattice>);

static_assert(LifetimeLattice::bottom() == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::top() == Lifetime::PER_FLEET);

static_assert(LifetimeLattice::name() == "LifetimeLattice");
static_assert(LifetimeLattice::At<Lifetime::PER_REQUEST>::name() == "LifetimeLattice::At<PER_REQUEST>");
static_assert(LifetimeLattice::At<Lifetime::PER_FLEET>::name() == "LifetimeLattice::At<PER_FLEET>");
static_assert(LifetimeLattice::At<static_cast<Lifetime>(255)>::name() == "LifetimeLattice::At<?>");
static_assert(lifetime_name(Lifetime::PER_PROGRAM) == "PER_PROGRAM");
static_assert(lifetime_name(static_cast<Lifetime>(255)) == "<unknown Lifetime>");

static_assert(lifetime::PerRequestTier::scope == Lifetime::PER_REQUEST);
static_assert(lifetime::PerFleetTier::scope == Lifetime::PER_FLEET);

}  // namespace detail::lifetime_lattice_self_test

}  // namespace foundation::algebra::lattices
