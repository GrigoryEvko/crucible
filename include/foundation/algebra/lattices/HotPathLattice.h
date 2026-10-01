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

enum class HotPathTier : std::uint8_t {
    Cold = 0,  // bottom: block / IO / syscall OK without bound
    Warm = 1,  // background-but-bounded: alloc OK, no syscall on hot loop
    Hot = 2,  // top: foreground hot path; no alloc / syscall / block
};

// A tighter budget is the stronger claim.
struct HotPathLattice : EnumChainLattice<HotPathLattice, HotPathTier, ClaimOrientation::stronger_is_higher> {
    template <HotPathTier T>
    struct At : PinnedAt<HotPathLattice, T> {
        static constexpr HotPathTier tier = T;
    };
};

namespace hot_path_tier {
using ColdTier = HotPathLattice::At<HotPathTier::Cold>;
using WarmTier = HotPathLattice::At<HotPathTier::Warm>;
using HotTier = HotPathLattice::At<HotPathTier::Hot>;
}  // namespace hot_path_tier

}  // namespace foundation::algebra::lattices
