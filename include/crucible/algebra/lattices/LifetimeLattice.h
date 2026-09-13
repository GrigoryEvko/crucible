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

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class Lifetime : std::uint8_t {
    PER_REQUEST = 0,  // destroyed on session close
    PER_PROGRAM = 1,  // shared across sibling sessions of the same program
    PER_FLEET = 2,  // consensus-replicated across the whole fleet
};

inline constexpr std::size_t lifetime_count = std::meta::enumerators_of(^^Lifetime).size();

[[nodiscard]] consteval std::string_view lifetime_name(Lifetime l) noexcept {
    switch (l) {
        case Lifetime::PER_REQUEST:
            return "PER_REQUEST";
        case Lifetime::PER_PROGRAM:
            return "PER_PROGRAM";
        case Lifetime::PER_FLEET:
            return "PER_FLEET";
        default:
            return std::string_view{"<unknown Lifetime>"};
    }
}

struct LifetimeLattice : ChainLatticeOps<Lifetime> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Lifetime::PER_REQUEST; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Lifetime::PER_FLEET; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "LifetimeLattice"; }

    template <Lifetime L>
    struct At {
        struct element_type {
            using lifetime_value_type = Lifetime;
            [[nodiscard]] constexpr operator lifetime_value_type() const noexcept { return L; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr Lifetime scope = L;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (L) {
                case Lifetime::PER_REQUEST:
                    return "LifetimeLattice::At<PER_REQUEST>";
                case Lifetime::PER_PROGRAM:
                    return "LifetimeLattice::At<PER_PROGRAM>";
                case Lifetime::PER_FLEET:
                    return "LifetimeLattice::At<PER_FLEET>";
                default:
                    return "LifetimeLattice::At<?>";
            }
        }
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

[[nodiscard]] consteval bool every_lifetime_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Lifetime));
    // `template for` unrolls into successive scopes that each declare the
    // induction variable, so -Wshadow fires on the body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (lifetime_name([:en:]) == std::string_view{"<unknown Lifetime>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_lifetime_has_name(), "lifetime_name() has no arm for at least one scope, so that scope "
                                         "reports the '<unknown Lifetime>' sentinel.");

static_assert(Lattice<LifetimeLattice>);
static_assert(BoundedLattice<LifetimeLattice>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_REQUEST>>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_PROGRAM>>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_FLEET>>);
static_assert(BoundedLattice<LifetimeLattice::At<Lifetime::PER_FLEET>>);

static_assert(!UnboundedLattice<LifetimeLattice>);
static_assert(!Semiring<LifetimeLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_REQUEST>::element_type>);
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_PROGRAM>::element_type>);
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_FLEET>::element_type>);

static_assert(verify_chain_lattice_exhaustive<LifetimeLattice>(),
              "LifetimeLattice's chain-order lattice axioms must hold at every "
              "(Lifetime)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<LifetimeLattice>(),
              "LifetimeLattice's chain order must satisfy distributivity at "
              "every (Lifetime)³ triple.");

static_assert(LifetimeLattice::leq(Lifetime::PER_REQUEST, Lifetime::PER_PROGRAM));
static_assert(LifetimeLattice::leq(Lifetime::PER_PROGRAM, Lifetime::PER_FLEET));
static_assert(LifetimeLattice::leq(Lifetime::PER_REQUEST, Lifetime::PER_FLEET));
static_assert(!LifetimeLattice::leq(Lifetime::PER_FLEET, Lifetime::PER_REQUEST));
static_assert(!LifetimeLattice::leq(Lifetime::PER_FLEET, Lifetime::PER_PROGRAM));
static_assert(!LifetimeLattice::leq(Lifetime::PER_PROGRAM, Lifetime::PER_REQUEST));

static_assert(LifetimeLattice::bottom() == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::top() == Lifetime::PER_FLEET);

static_assert(LifetimeLattice::join(Lifetime::PER_REQUEST, Lifetime::PER_FLEET) == Lifetime::PER_FLEET);
static_assert(LifetimeLattice::join(Lifetime::PER_PROGRAM, Lifetime::PER_REQUEST) == Lifetime::PER_PROGRAM);
static_assert(LifetimeLattice::meet(Lifetime::PER_REQUEST, Lifetime::PER_FLEET) == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::meet(Lifetime::PER_PROGRAM, Lifetime::PER_FLEET) == Lifetime::PER_PROGRAM);

static_assert(LifetimeLattice::name() == "LifetimeLattice");
static_assert(LifetimeLattice::At<Lifetime::PER_REQUEST>::name() == "LifetimeLattice::At<PER_REQUEST>");
static_assert(LifetimeLattice::At<Lifetime::PER_PROGRAM>::name() == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(LifetimeLattice::At<Lifetime::PER_FLEET>::name() == "LifetimeLattice::At<PER_FLEET>");
static_assert(lifetime_name(Lifetime::PER_REQUEST) == "PER_REQUEST");
static_assert(lifetime_name(Lifetime::PER_PROGRAM) == "PER_PROGRAM");
static_assert(lifetime_name(Lifetime::PER_FLEET) == "PER_FLEET");

[[nodiscard]] consteval bool every_at_lifetime_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Lifetime));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (LifetimeLattice::At<([:en:])>::name() == std::string_view{"LifetimeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_lifetime_has_name(), "LifetimeLattice::At<L>::name() has no arm for at least one scope, "
                                            "so that scope reports the 'LifetimeLattice::At<?>' sentinel.");

static_assert(lifetime::PerRequestTier::scope == Lifetime::PER_REQUEST);
static_assert(lifetime::PerProgramTier::scope == Lifetime::PER_PROGRAM);
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

}  // namespace crucible::algebra::lattices
