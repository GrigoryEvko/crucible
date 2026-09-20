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

enum class HotPathTier : std::uint8_t {
    Cold = 0,  // bottom: block / IO / syscall OK without bound
    Warm = 1,  // background-but-bounded: alloc OK, no syscall on hot loop
    Hot = 2,  // top: foreground hot path; no alloc / syscall / block
};

inline constexpr std::size_t hot_path_tier_count = ::foundation::reflect::enum_count<HotPathTier>;

// The identifier of t, or "<unknown HotPathTier>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view hot_path_tier_name(HotPathTier t) noexcept {
    return ::foundation::reflect::enum_name(t);
}

struct HotPathLattice : ChainLatticeOps<HotPathTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return HotPathTier::Cold; }
    [[nodiscard]] static constexpr element_type top() noexcept { return HotPathTier::Hot; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "HotPathLattice"; }

    template <HotPathTier T>
    struct AtElement : PinnedElement<T> {
        using hot_path_tier_value_type = HotPathTier;
    };

    template <HotPathTier T>
    struct At : PinnedAt<HotPathLattice, T, AtElement<T>> {
        static constexpr HotPathTier tier = T;
    };
};

namespace hot_path_tier {
using ColdTier = HotPathLattice::At<HotPathTier::Cold>;
using WarmTier = HotPathLattice::At<HotPathTier::Warm>;
using HotTier = HotPathLattice::At<HotPathTier::Hot>;
}  // namespace hot_path_tier

namespace detail::hot_path_lattice_self_test {

static_assert(hot_path_tier_count == 3, "HotPathTier must hold exactly the three tiers Cold, Warm and Hot.");

static_assert(verify_chain_lattice<HotPathLattice>(), "HotPathLattice: the chain order, the pinned grades or the "
                                                      "reflected names diverged from the HotPathTier enumerator list.");

static_assert(!UnboundedLattice<HotPathLattice>);
static_assert(!Semiring<HotPathLattice>);

static_assert(HotPathLattice::bottom() == HotPathTier::Cold);
static_assert(HotPathLattice::top() == HotPathTier::Hot);

static_assert(HotPathLattice::name() == "HotPathLattice");
static_assert(hot_path_tier::ColdTier::name() == "HotPathLattice::At<Cold>");
static_assert(hot_path_tier::HotTier::name() == "HotPathLattice::At<Hot>");
static_assert(HotPathLattice::At<static_cast<HotPathTier>(255)>::name() == "HotPathLattice::At<?>");

static_assert(hot_path_tier_name(HotPathTier::Warm) == "Warm");
static_assert(hot_path_tier_name(static_cast<HotPathTier>(255)) == "<unknown HotPathTier>");

static_assert(hot_path_tier::ColdTier::tier == HotPathTier::Cold);
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

}  // namespace detail::hot_path_lattice_self_test

}  // namespace foundation::algebra::lattices
