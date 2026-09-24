#pragma once

// Three-tier chain over how hot a value's access pattern is: which
// level of the cache hierarchy holds its working set.  Not where the
// value durably lives, and not what a function is permitted to do.
//
// The hottest residency sits at the top, so `leq(weak, strong)` reads "a
// weaker-heat consumer is satisfied by a stronger-heat provider".  A Hot
// value is admissible everywhere, because it is the most-cached claim
// available.
//
// A structurally identical chain grades a different axis and stays a
// separate type.  The axes are independent, so the grades must never
// collapse into one.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class ResidencyHeatTag : std::uint8_t {
    Cold = 0,  // bottom: L3 or DRAM working-set tail
    Warm = 1,  // L2 working-set body
    Hot = 2,  // top: L1 hottest working-set
};

inline constexpr std::size_t residency_heat_tag_count = std::meta::enumerators_of(^^ResidencyHeatTag).size();

[[nodiscard]] consteval std::string_view residency_heat_tag_name(ResidencyHeatTag t) noexcept {
    switch (t) {
        case ResidencyHeatTag::Cold:
            return "Cold";
        case ResidencyHeatTag::Warm:
            return "Warm";
        case ResidencyHeatTag::Hot:
            return "Hot";
        default:
            return std::string_view{"<unknown ResidencyHeatTag>"};
    }
}

struct ResidencyHeatLattice : ChainLatticeOps<ResidencyHeatTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return ResidencyHeatTag::Cold; }
    [[nodiscard]] static constexpr element_type top() noexcept { return ResidencyHeatTag::Hot; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ResidencyHeatLattice"; }

    template <ResidencyHeatTag T>
    struct At {
        struct element_type {
            using residency_heat_tag_value_type = ResidencyHeatTag;
            [[nodiscard]] constexpr operator residency_heat_tag_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr ResidencyHeatTag tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ResidencyHeatTag::Cold:
                    return "ResidencyHeatLattice::At<Cold>";
                case ResidencyHeatTag::Warm:
                    return "ResidencyHeatLattice::At<Warm>";
                case ResidencyHeatTag::Hot:
                    return "ResidencyHeatLattice::At<Hot>";
                default:
                    return "ResidencyHeatLattice::At<?>";
            }
        }
    };
};

namespace residency_heat_tag {
using ColdHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Cold>;
using WarmHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Warm>;
using HotHeat = ResidencyHeatLattice::At<ResidencyHeatTag::Hot>;
}  // namespace residency_heat_tag

namespace detail::residency_heat_lattice_self_test {

static_assert(residency_heat_tag_count == 3, "ResidencyHeatTag must hold exactly the three tiers Cold, Warm "
                                             "and Hot.");

[[nodiscard]] consteval bool every_residency_heat_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResidencyHeatTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (residency_heat_tag_name([:en:]) == std::string_view{"<unknown ResidencyHeatTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_residency_heat_tag_has_name(),
              "residency_heat_tag_name() has no arm for at least one tier, so that tier reports the "
              "'<unknown ResidencyHeatTag>' sentinel.");

static_assert(Lattice<ResidencyHeatLattice>);
static_assert(BoundedLattice<ResidencyHeatLattice>);
static_assert(Lattice<residency_heat_tag::ColdHeat>);
static_assert(Lattice<residency_heat_tag::WarmHeat>);
static_assert(Lattice<residency_heat_tag::HotHeat>);
static_assert(BoundedLattice<residency_heat_tag::HotHeat>);

static_assert(!UnboundedLattice<ResidencyHeatLattice>);
static_assert(!Semiring<ResidencyHeatLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<residency_heat_tag::ColdHeat::element_type>);
static_assert(std::is_empty_v<residency_heat_tag::WarmHeat::element_type>);
static_assert(std::is_empty_v<residency_heat_tag::HotHeat::element_type>);

static_assert(verify_chain_lattice_exhaustive<ResidencyHeatLattice>(),
              "ResidencyHeatLattice's chain-order axioms must hold at every "
              "(ResidencyHeatTag)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<ResidencyHeatLattice>(),
              "ResidencyHeatLattice's chain order must satisfy distributivity at "
              "every (ResidencyHeatTag)³ triple.");

static_assert(ResidencyHeatLattice::leq(ResidencyHeatTag::Cold, ResidencyHeatTag::Warm));
static_assert(ResidencyHeatLattice::leq(ResidencyHeatTag::Warm, ResidencyHeatTag::Hot));
static_assert(ResidencyHeatLattice::leq(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Hot, ResidencyHeatTag::Cold));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Hot, ResidencyHeatTag::Warm));
static_assert(!ResidencyHeatLattice::leq(ResidencyHeatTag::Warm, ResidencyHeatTag::Cold));

static_assert(ResidencyHeatLattice::bottom() == ResidencyHeatTag::Cold);
static_assert(ResidencyHeatLattice::top() == ResidencyHeatTag::Hot);

static_assert(ResidencyHeatLattice::join(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot) == ResidencyHeatTag::Hot);
static_assert(ResidencyHeatLattice::join(ResidencyHeatTag::Warm, ResidencyHeatTag::Cold) == ResidencyHeatTag::Warm);
static_assert(ResidencyHeatLattice::meet(ResidencyHeatTag::Cold, ResidencyHeatTag::Hot) == ResidencyHeatTag::Cold);
static_assert(ResidencyHeatLattice::meet(ResidencyHeatTag::Warm, ResidencyHeatTag::Hot) == ResidencyHeatTag::Warm);

static_assert(ResidencyHeatLattice::name() == "ResidencyHeatLattice");
static_assert(residency_heat_tag::ColdHeat::name() == "ResidencyHeatLattice::At<Cold>");
static_assert(residency_heat_tag::WarmHeat::name() == "ResidencyHeatLattice::At<Warm>");
static_assert(residency_heat_tag::HotHeat::name() == "ResidencyHeatLattice::At<Hot>");

[[nodiscard]] consteval bool every_at_residency_heat_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResidencyHeatTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ResidencyHeatLattice::At<([:en:])>::name() == std::string_view{"ResidencyHeatLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_residency_heat_tag_has_name(),
              "ResidencyHeatLattice::At<T>::name() has no arm for at least one tier, so that tier reports the "
              "'ResidencyHeatLattice::At<?>' sentinel.");

static_assert(residency_heat_tag::ColdHeat::tier == ResidencyHeatTag::Cold);
static_assert(residency_heat_tag::WarmHeat::tier == ResidencyHeatTag::Warm);
static_assert(residency_heat_tag::HotHeat::tier == ResidencyHeatTag::Hot);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top tier witnesses the collapse for both class and arithmetic
// values; the other two need only one witness each.
template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, residency_heat_tag::HotHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, residency_heat_tag::WarmHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, residency_heat_tag::ColdHeat, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

inline void runtime_smoke_test() {
    ResidencyHeatTag a = ResidencyHeatTag::Cold;
    ResidencyHeatTag b = ResidencyHeatTag::Hot;
    [[maybe_unused]] bool l1 = ResidencyHeatLattice::leq(a, b);
    [[maybe_unused]] ResidencyHeatTag j1 = ResidencyHeatLattice::join(a, b);
    [[maybe_unused]] ResidencyHeatTag m1 = ResidencyHeatLattice::meet(a, b);
    [[maybe_unused]] ResidencyHeatTag bot = ResidencyHeatLattice::bottom();
    [[maybe_unused]] ResidencyHeatTag topv = ResidencyHeatLattice::top();

    ResidencyHeatTag warm = ResidencyHeatTag::Warm;
    [[maybe_unused]] ResidencyHeatTag j2 = ResidencyHeatLattice::join(warm, a);
    [[maybe_unused]] ResidencyHeatTag m2 = ResidencyHeatLattice::meet(warm, b);

    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, residency_heat_tag::HotHeat::bottom()};
    auto widened = initial.weaken(residency_heat_tag::HotHeat::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(residency_heat_tag::HotHeat::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    residency_heat_tag::HotHeat::element_type e{};
    [[maybe_unused]] ResidencyHeatTag rec = e;
}

}  // namespace detail::residency_heat_lattice_self_test

}  // namespace crucible::algebra::lattices
