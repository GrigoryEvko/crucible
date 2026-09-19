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
//
// Old spelling: include/crucible/algebra/lattices/LifetimeLattice.h.

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

struct LifetimeLattice : ChainLatticeOps<Lifetime> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Lifetime::PER_REQUEST; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Lifetime::PER_FLEET; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "LifetimeLattice"; }

    template <Lifetime L>
    struct AtElement : PinnedElement<L> {
        using lifetime_value_type = Lifetime;
    };

    template <Lifetime L>
    struct At : PinnedAt<LifetimeLattice, L, AtElement<L>> {
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

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top scope witnesses the collapse for both class and arithmetic
// values; the other scopes need only one witness each.
template <typename T>
using FleetOpaque = Graded<ModalityKind::Comonad, lifetime::PerFleetTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, double);

template <typename T>
using ProgramOpaque = Graded<ModalityKind::Comonad, lifetime::PerProgramTier, T>;
template <typename T>
using RequestOpaque = Graded<ModalityKind::Comonad, lifetime::PerRequestTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramOpaque, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestOpaque, EightByteValue);

inline void runtime_smoke_test() {
    Lifetime a = Lifetime::PER_REQUEST;
    Lifetime b = Lifetime::PER_FLEET;
    [[maybe_unused]] bool l1 = LifetimeLattice::leq(a, b);
    [[maybe_unused]] Lifetime j1 = LifetimeLattice::join(a, b);
    [[maybe_unused]] Lifetime m1 = LifetimeLattice::meet(a, b);
    [[maybe_unused]] Lifetime bot = LifetimeLattice::bottom();
    [[maybe_unused]] Lifetime top = LifetimeLattice::top();

    OneByteValue v{42};
    FleetOpaque<OneByteValue> initial{v, lifetime::PerFleetTier::bottom()};
    auto widened = initial.weaken(lifetime::PerFleetTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(lifetime::PerFleetTier::top());

    // extract() is reachable only because the modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    lifetime::PerFleetTier::element_type e{};
    [[maybe_unused]] Lifetime rec = e;
}

}  // namespace detail::lifetime_lattice_self_test

}  // namespace foundation::algebra::lattices
