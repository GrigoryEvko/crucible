#pragma once

// Three-tier chain over the work-budget envelope of a function: which
// operations it is permitted to perform.  Not where its data durably
// lives, and not how hot its access pattern is.
//
// The strongest budget sits at the top, so `leq(weak, strong)` reads "a
// weaker-budget consumer is satisfied by a stronger-budget provider".  A
// Hot function is admissible from every context.  A Cold one is
// admissible only from a Cold context.
//
// A structurally identical chain grades a different axis and stays a
// separate type.  The axes are independent, so the grades must never
// collapse into one.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class HotPathTier : std::uint8_t {
    Cold = 0,  // bottom: block / IO / syscall OK without bound
    Warm = 1,  // background-but-bounded: alloc OK, no syscall on hot loop
    Hot = 2,  // top: foreground hot path; no alloc / syscall / block
};

inline constexpr std::size_t hot_path_tier_count = std::meta::enumerators_of(^^HotPathTier).size();

[[nodiscard]] consteval std::string_view hot_path_tier_name(HotPathTier t) noexcept {
    switch (t) {
        case HotPathTier::Cold:
            return "Cold";
        case HotPathTier::Warm:
            return "Warm";
        case HotPathTier::Hot:
            return "Hot";
        default:
            return std::string_view{"<unknown HotPathTier>"};
    }
}

struct HotPathLattice : ChainLatticeOps<HotPathTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return HotPathTier::Cold; }
    [[nodiscard]] static constexpr element_type top() noexcept { return HotPathTier::Hot; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "HotPathLattice"; }

    template <HotPathTier T>
    struct At {
        struct element_type {
            using hot_path_tier_value_type = HotPathTier;
            [[nodiscard]] constexpr operator hot_path_tier_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr HotPathTier tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case HotPathTier::Cold:
                    return "HotPathLattice::At<Cold>";
                case HotPathTier::Warm:
                    return "HotPathLattice::At<Warm>";
                case HotPathTier::Hot:
                    return "HotPathLattice::At<Hot>";
                default:
                    return "HotPathLattice::At<?>";
            }
        }
    };
};

namespace hot_path_tier {
using ColdTier = HotPathLattice::At<HotPathTier::Cold>;
using WarmTier = HotPathLattice::At<HotPathTier::Warm>;
using HotTier = HotPathLattice::At<HotPathTier::Hot>;
}  // namespace hot_path_tier

namespace detail::hot_path_lattice_self_test {

static_assert(hot_path_tier_count == 3, "HotPathTier must hold exactly the three tiers Cold, Warm and Hot.");

[[nodiscard]] consteval bool every_hot_path_tier_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^HotPathTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (hot_path_tier_name([:en:]) == std::string_view{"<unknown HotPathTier>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_hot_path_tier_has_name(), "hot_path_tier_name() has no arm for at least one tier, so that "
                                              "tier reports the '<unknown HotPathTier>' sentinel.");

static_assert(Lattice<HotPathLattice>);
static_assert(BoundedLattice<HotPathLattice>);
static_assert(Lattice<hot_path_tier::ColdTier>);
static_assert(Lattice<hot_path_tier::WarmTier>);
static_assert(Lattice<hot_path_tier::HotTier>);
static_assert(BoundedLattice<hot_path_tier::HotTier>);

static_assert(!UnboundedLattice<HotPathLattice>);
static_assert(!Semiring<HotPathLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<hot_path_tier::ColdTier::element_type>);
static_assert(std::is_empty_v<hot_path_tier::WarmTier::element_type>);
static_assert(std::is_empty_v<hot_path_tier::HotTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<HotPathLattice>(),
              "HotPathLattice's chain-order lattice axioms must hold at every "
              "(HotPathTier)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<HotPathLattice>(),
              "HotPathLattice's chain order must satisfy distributivity at every "
              "(HotPathTier)³ triple.");

static_assert(HotPathLattice::leq(HotPathTier::Cold, HotPathTier::Warm));
static_assert(HotPathLattice::leq(HotPathTier::Warm, HotPathTier::Hot));
static_assert(HotPathLattice::leq(HotPathTier::Cold, HotPathTier::Hot));
static_assert(!HotPathLattice::leq(HotPathTier::Hot, HotPathTier::Cold));
static_assert(!HotPathLattice::leq(HotPathTier::Hot, HotPathTier::Warm));
static_assert(!HotPathLattice::leq(HotPathTier::Warm, HotPathTier::Cold));

static_assert(HotPathLattice::bottom() == HotPathTier::Cold);
static_assert(HotPathLattice::top() == HotPathTier::Hot);

static_assert(HotPathLattice::join(HotPathTier::Cold, HotPathTier::Hot) == HotPathTier::Hot);
static_assert(HotPathLattice::join(HotPathTier::Warm, HotPathTier::Cold) == HotPathTier::Warm);
static_assert(HotPathLattice::meet(HotPathTier::Cold, HotPathTier::Hot) == HotPathTier::Cold);
static_assert(HotPathLattice::meet(HotPathTier::Warm, HotPathTier::Hot) == HotPathTier::Warm);

static_assert(HotPathLattice::name() == "HotPathLattice");
static_assert(hot_path_tier::ColdTier::name() == "HotPathLattice::At<Cold>");
static_assert(hot_path_tier::WarmTier::name() == "HotPathLattice::At<Warm>");
static_assert(hot_path_tier::HotTier::name() == "HotPathLattice::At<Hot>");

[[nodiscard]] consteval bool every_at_hot_path_tier_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^HotPathTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (HotPathLattice::At<([:en:])>::name() == std::string_view{"HotPathLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_hot_path_tier_has_name(), "HotPathLattice::At<T>::name() has no arm for at least one "
                                                 "tier, so that tier reports the 'HotPathLattice::At<?>' "
                                                 "sentinel.");

static_assert(hot_path_tier::ColdTier::tier == HotPathTier::Cold);
static_assert(hot_path_tier::WarmTier::tier == HotPathTier::Warm);
static_assert(hot_path_tier::HotTier::tier == HotPathTier::Hot);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top tier witnesses the collapse for both class and arithmetic
// values; the other two need only one witness each.
template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, hot_path_tier::HotTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, hot_path_tier::WarmTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, hot_path_tier::ColdTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

inline void runtime_smoke_test() {
    HotPathTier a = HotPathTier::Cold;
    HotPathTier b = HotPathTier::Hot;
    [[maybe_unused]] bool l1 = HotPathLattice::leq(a, b);
    [[maybe_unused]] HotPathTier j1 = HotPathLattice::join(a, b);
    [[maybe_unused]] HotPathTier m1 = HotPathLattice::meet(a, b);
    [[maybe_unused]] HotPathTier bot = HotPathLattice::bottom();
    [[maybe_unused]] HotPathTier topv = HotPathLattice::top();

    HotPathTier warm = HotPathTier::Warm;
    [[maybe_unused]] HotPathTier j2 = HotPathLattice::join(warm, a);
    [[maybe_unused]] HotPathTier m2 = HotPathLattice::meet(warm, b);

    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, hot_path_tier::HotTier::bottom()};
    auto widened = initial.weaken(hot_path_tier::HotTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(hot_path_tier::HotTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    hot_path_tier::HotTier::element_type e{};
    [[maybe_unused]] HotPathTier rec = e;
}

}  // namespace detail::hot_path_lattice_self_test

}  // namespace crucible::algebra::lattices
