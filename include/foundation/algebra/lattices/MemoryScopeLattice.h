#pragma once

// Two chains that share a bottom and a top, not one chain.  The accelerator
// scopes and the host shareability domains order internally but have no
// relation to one another: a fence at block scope on a GPU neither subsumes nor
// is subsumed by an inner-shareable barrier on the host.
//
// The two chains meet at a single top because full-system visibility is one
// concept, not two.  Once every observer on the machine can see a value, there
// is no further distinction between a full-system accelerator fence and a
// full-system host barrier.
//
// Wider visibility sits higher, so a leq that holds reads as a value needing
// the lower scope being published by a fence at the higher one.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// The high nibble names the chain and the low nibble the rank within it, so
// comparing the underlying integers of two values from the same chain gives
// their order directly.  Across chains the integers mean nothing.
//
// Thread and Warp have no instruction of their own.  The instruction set spells
// only the block, cluster, device and system scopes.  The two narrower levels
// exist so that a publication can state that no cross-thread fence is needed at
// all, and lowering emits nothing for them.
enum class MemoryScope : std::uint8_t {
    Thread = 0x00,  // visible only to the issuing thread
    Warp = 0x10,  // warp-coherent, reached through warp-sync primitives
    Cta = 0x11,  // thread block, spelled `.cta`
    Cluster = 0x12,  // thread-block cluster, spelled `.cluster`
    Gpu = 0x13,  // device-wide, spelled `.gpu`
    Inner = 0x20,  // inner-shareable domain, DMB ISH
    Outer = 0x21,  // outer-shareable domain, DMB OSH
    System = 0xFF,  // everything on the machine: `.sys`, or DMB SY
};

namespace detail {

// The chain that holds x: the high nibble of its value, 1 for the
// accelerator scopes and 2 for the host domains.  Thread, System and a
// value outside the enum hold no chain, and the answer is 0 for them.
//
// The switch names each enumerator, so a value outside the enum takes the
// default.  The function reads no reflection, so an includer evaluates
// nothing.  The check file of this header walks the enumerators by
// reflection and refuses an enumerator that the switch does not name.
[[nodiscard]] constexpr std::uint8_t mem_scope_chain(MemoryScope x) noexcept {
    switch (x) {
        case MemoryScope::Thread:
        case MemoryScope::Warp:
        case MemoryScope::Cta:
        case MemoryScope::Cluster:
        case MemoryScope::Gpu:
        case MemoryScope::Inner:
        case MemoryScope::Outer:
        case MemoryScope::System: {
            const auto nibble = static_cast<std::uint8_t>(std::to_underlying(x) >> 4);
            return nibble == 1 || nibble == 2 ? nibble : std::uint8_t{0};
        }
        default:
            return 0;
    }
}

}  // namespace detail

[[nodiscard]] constexpr bool mem_scope_is_accel(MemoryScope x) noexcept { return detail::mem_scope_chain(x) == 1; }
[[nodiscard]] constexpr bool mem_scope_is_arm(MemoryScope x) noexcept { return detail::mem_scope_chain(x) == 2; }
// Thread and System belong to neither chain, so this answers false for both of
// them against anything.  Every caller therefore has to settle those two cases
// before asking.
[[nodiscard]] constexpr bool mem_scope_same_trunk(MemoryScope a, MemoryScope b) noexcept {
    return (mem_scope_is_accel(a) && mem_scope_is_accel(b)) || (mem_scope_is_arm(a) && mem_scope_is_arm(b));
}

struct MemoryScopeLattice {
    using element_type = MemoryScope;

    // A wider visibility is the stronger claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return MemoryScope::Thread; }
    [[nodiscard]] static constexpr element_type top() noexcept { return MemoryScope::System; }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == MemoryScope::Thread) return true;
        if (b == MemoryScope::System) return true;
        if (mem_scope_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b);
        }
        return false;
    }

    // Two scopes from different chains have nothing between them, so their
    // least upper bound can only be the top and their greatest lower bound only
    // the bottom.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == MemoryScope::Thread) return b;
        if (b == MemoryScope::Thread) return a;
        if (a == MemoryScope::System || b == MemoryScope::System) {
            return MemoryScope::System;
        }
        if (mem_scope_same_trunk(a, b)) {
            return std::to_underlying(a) >= std::to_underlying(b) ? a : b;
        }
        return MemoryScope::System;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == MemoryScope::System) return b;
        if (b == MemoryScope::System) return a;
        if (a == MemoryScope::Thread || b == MemoryScope::Thread) {
            return MemoryScope::Thread;
        }
        if (mem_scope_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b) ? a : b;
        }
        return MemoryScope::Thread;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "MemoryScopeLattice"; }

    template <MemoryScope S>
    struct At : PinnedAt<MemoryScopeLattice, S> {
        static constexpr MemoryScope scope = S;
    };
};

namespace memory_scope {
using ThreadScope = MemoryScopeLattice::At<MemoryScope::Thread>;
using WarpScope = MemoryScopeLattice::At<MemoryScope::Warp>;
using CtaScope = MemoryScopeLattice::At<MemoryScope::Cta>;
using ClusterScope = MemoryScopeLattice::At<MemoryScope::Cluster>;
using GpuScope = MemoryScopeLattice::At<MemoryScope::Gpu>;
using InnerScope = MemoryScopeLattice::At<MemoryScope::Inner>;
using OuterScope = MemoryScopeLattice::At<MemoryScope::Outer>;
using SystemScope = MemoryScopeLattice::At<MemoryScope::System>;
}  // namespace memory_scope

}  // namespace foundation::algebra::lattices
