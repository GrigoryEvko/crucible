#pragma once

// Four-tier chain over how a function touches global and static mutable
// state.  Declaring a tier asserts that the actual set of global
// interactions is contained in the set that tier allows.
//
// The hazard grows upward, which inverts the usual reading of the
// operators.  A join returns the hazardier of two tiers, which is right
// for propagation: a region containing a hazardful function inherits the
// hazard.  It is wrong for admission.  A gate that wants the tightest
// policy every participant claims must call meet, because join hands
// back the most permissive party's tier.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class GlobalState : std::uint8_t {
    Stateless = 0,  // bottom — no global/static mutable interaction
    ConstGlobal = 1,  // reads const / constinit globals only
    MutableGlobal = 2,  // reads/writes synchronized or thread-local mutable globals
    InitOrderHazard = 3,  // top — static-init-order or lazy-init hazard
};

[[nodiscard]] consteval std::string_view global_state_name(GlobalState t) noexcept {
    switch (t) {
        case GlobalState::Stateless:
            return "Stateless";
        case GlobalState::ConstGlobal:
            return "ConstGlobal";
        case GlobalState::MutableGlobal:
            return "MutableGlobal";
        case GlobalState::InitOrderHazard:
            return "InitOrderHazard";
        default:
            return std::string_view{"<unknown GlobalState>"};
    }
}

struct GlobalStateLattice : ChainLatticeOps<GlobalState> {
    [[nodiscard]] static constexpr GlobalState bottom() noexcept { return GlobalState::Stateless; }
    [[nodiscard]] static constexpr GlobalState top() noexcept { return GlobalState::InitOrderHazard; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "GlobalStateLattice"; }

    template <GlobalState T>
    struct At {
        struct element_type {
            using global_state_value_type = GlobalState;
            [[nodiscard]] constexpr operator global_state_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr GlobalState tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case GlobalState::Stateless:
                    return "GlobalStateLattice::At<Stateless>";
                case GlobalState::ConstGlobal:
                    return "GlobalStateLattice::At<ConstGlobal>";
                case GlobalState::MutableGlobal:
                    return "GlobalStateLattice::At<MutableGlobal>";
                case GlobalState::InitOrderHazard:
                    return "GlobalStateLattice::At<InitOrderHazard>";
                default:
                    return "GlobalStateLattice::At<?>";
            }
        }
    };
};

namespace detail::global_state_lattice_self_test {

inline constexpr std::size_t global_state_count = std::meta::enumerators_of(^^GlobalState).size();

static_assert(global_state_count == 4, "GlobalState must hold exactly the four tiers Stateless, ConstGlobal, "
                                       "MutableGlobal and InitOrderHazard.  A new tier appends at the next "
                                       "free ordinal and needs an arm in global_state_name and in At<T>'s "
                                       "name().");

static_assert(std::to_underlying(GlobalState::Stateless) == 0);
static_assert(std::to_underlying(GlobalState::InitOrderHazard) == 3);
static_assert(std::is_same_v<std::underlying_type_t<GlobalState>, std::uint8_t>);

[[nodiscard]] consteval bool every_global_state_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^GlobalState));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = global_state_name([:en:]);
        if (n == std::string_view{"<unknown GlobalState>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_global_state_has_name(), "global_state_name() has no arm for at least one tier, so that "
                                             "tier reports the '<unknown GlobalState>' sentinel.");

static_assert(::crucible::algebra::Lattice<GlobalStateLattice>);
static_assert(::crucible::algebra::BoundedLattice<GlobalStateLattice>);
static_assert(!::crucible::algebra::Semiring<GlobalStateLattice>);

static_assert(verify_chain_lattice_exhaustive<GlobalStateLattice>(),
              "GlobalStateLattice's chain-order lattice axioms must hold at every "
              "(GlobalState)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<GlobalStateLattice>(),
              "GlobalStateLattice's chain order must satisfy distributivity at every "
              "(GlobalState)³ triple.");

static_assert(GlobalStateLattice::bottom() == GlobalState::Stateless);
static_assert(GlobalStateLattice::top() == GlobalState::InitOrderHazard);
static_assert(GlobalStateLattice::name() == std::string_view{"GlobalStateLattice"});

static_assert(GlobalStateLattice::leq(GlobalState::Stateless, GlobalState::InitOrderHazard));
static_assert(!GlobalStateLattice::leq(GlobalState::InitOrderHazard, GlobalState::Stateless));

static_assert(GlobalStateLattice::leq(GlobalState::Stateless, GlobalState::ConstGlobal));
static_assert(GlobalStateLattice::leq(GlobalState::ConstGlobal, GlobalState::MutableGlobal));
static_assert(GlobalStateLattice::leq(GlobalState::MutableGlobal, GlobalState::InitOrderHazard));

static_assert(!GlobalStateLattice::leq(GlobalState::ConstGlobal, GlobalState::Stateless));
static_assert(!GlobalStateLattice::leq(GlobalState::InitOrderHazard, GlobalState::MutableGlobal));

static_assert(GlobalStateLattice::join(GlobalState::ConstGlobal, GlobalState::MutableGlobal)
              == GlobalState::MutableGlobal);
static_assert(GlobalStateLattice::join(GlobalState::Stateless, GlobalState::ConstGlobal) == GlobalState::ConstGlobal);
static_assert(GlobalStateLattice::meet(GlobalState::InitOrderHazard, GlobalState::ConstGlobal)
              == GlobalState::ConstGlobal);

// The two assertions below pin the polarity.  Inverting the chain reds
// them in lockstep and stops a gate from being written against the wrong
// operator.
static_assert(GlobalStateLattice::join(GlobalState::Stateless, GlobalState::InitOrderHazard)
                  == GlobalState::InitOrderHazard,
              "GlobalStateLattice's join returns the loosest hazard policy of "
              "the two operands.  A gate that treats composition as "
              "strictest-wins would silently admit InitOrderHazard.  Use meet "
              "for the tightest policy.");
static_assert(GlobalStateLattice::meet(GlobalState::Stateless, GlobalState::InitOrderHazard) == GlobalState::Stateless,
              "GlobalStateLattice's meet returns the strictest hazard policy of "
              "the two operands.  An admission gate that wants only the tightest "
              "policy every participant claims must call meet, not join.");

static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::Stateless>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::ConstGlobal>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::MutableGlobal>::element_type>);
static_assert(std::is_empty_v<GlobalStateLattice::At<GlobalState::InitOrderHazard>::element_type>);
static_assert(GlobalStateLattice::At<GlobalState::MutableGlobal>::tier == GlobalState::MutableGlobal);

inline void global_state_lattice_runtime_smoke_test() {
    GlobalState a = GlobalState::Stateless;
    GlobalState b = GlobalState::InitOrderHazard;
    [[maybe_unused]] bool rl = GlobalStateLattice::leq(a, b);
    [[maybe_unused]] GlobalState rj = GlobalStateLattice::join(a, b);
    [[maybe_unused]] GlobalState rm = GlobalStateLattice::meet(a, b);

    GlobalState c = GlobalState::ConstGlobal;
    GlobalState d = GlobalState::MutableGlobal;
    [[maybe_unused]] GlobalState rj2 = GlobalStateLattice::join(c, d);
    [[maybe_unused]] GlobalState rm2 = GlobalStateLattice::meet(c, d);

    GlobalStateLattice::At<GlobalState::MutableGlobal>::element_type mg_pin{};
    [[maybe_unused]] GlobalState mg_recovered = mg_pin;
}

}  // namespace detail::global_state_lattice_self_test

}  // namespace crucible::algebra::lattices
