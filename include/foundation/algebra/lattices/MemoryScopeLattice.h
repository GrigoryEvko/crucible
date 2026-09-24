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

inline constexpr std::size_t memory_scope_count = ::foundation::reflect::enum_count<MemoryScope>;

// The identifier of x, or "<unknown MemoryScope>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view memory_scope_name(MemoryScope x) noexcept {
    return ::foundation::reflect::enum_name(x);
}

[[nodiscard]] constexpr bool mem_scope_is_accel(MemoryScope x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(MemoryScope::Warp) && u <= std::to_underlying(MemoryScope::Gpu);
}
[[nodiscard]] constexpr bool mem_scope_is_arm(MemoryScope x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(MemoryScope::Inner) && u <= std::to_underlying(MemoryScope::Outer);
}
// Thread and System belong to neither chain, so this answers false for both of
// them against anything.  Every caller therefore has to settle those two cases
// before asking.
[[nodiscard]] constexpr bool mem_scope_same_trunk(MemoryScope a, MemoryScope b) noexcept {
    return (mem_scope_is_accel(a) && mem_scope_is_accel(b)) || (mem_scope_is_arm(a) && mem_scope_is_arm(b));
}

struct MemoryScopeLattice {
    using element_type = MemoryScope;

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
    struct AtElement : PinnedElement<S> {
        using memory_scope_value_type = MemoryScope;
    };

    template <MemoryScope S>
    struct At : PinnedAt<MemoryScopeLattice, S, AtElement<S>> {
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

namespace detail::memory_scope_lattice_self_test {

static_assert(memory_scope_count == 8, "The MemoryScope catalog changed size.  Confirm the intent, then update "
                                       "mem_scope_is_accel and mem_scope_is_arm, which bound the two chains by "
                                       "underlying value.");

static_assert(Lattice<MemoryScopeLattice>);
static_assert(BoundedLattice<MemoryScopeLattice>);
static_assert(!UnboundedLattice<MemoryScopeLattice>);
static_assert(!Semiring<MemoryScopeLattice>);

static_assert(MemoryScopeLattice::bottom() == MemoryScope::Thread);
static_assert(MemoryScopeLattice::top() == MemoryScope::System);

static_assert(mem_scope_is_accel(MemoryScope::Warp));
static_assert(mem_scope_is_accel(MemoryScope::Gpu));
static_assert(!mem_scope_is_accel(MemoryScope::Inner));
static_assert(!mem_scope_is_accel(MemoryScope::Thread));
static_assert(!mem_scope_is_accel(MemoryScope::System));
static_assert(mem_scope_is_arm(MemoryScope::Inner));
static_assert(mem_scope_is_arm(MemoryScope::Outer));
static_assert(!mem_scope_is_arm(MemoryScope::Cta));
static_assert(!mem_scope_is_arm(MemoryScope::Thread));
static_assert(!mem_scope_is_arm(MemoryScope::System));
static_assert(mem_scope_same_trunk(MemoryScope::Warp, MemoryScope::Gpu));
static_assert(mem_scope_same_trunk(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!mem_scope_same_trunk(MemoryScope::Cta, MemoryScope::Inner));
static_assert(!mem_scope_same_trunk(MemoryScope::Thread, MemoryScope::Inner));
static_assert(!mem_scope_same_trunk(MemoryScope::System, MemoryScope::Cta));

// The partial-order axioms at every triple, and leq, join and meet in
// agreement at every pair, walked by reflection over the enumerators.
static_assert(verify_enum_lattice_exhaustive<MemoryScopeLattice>(),
              "The partial-order axioms must hold at every triple of the eight "
              "scopes.  A failure means leq, join or meet is wrong for some pair, or "
              "the routing between Thread, System, same-chain rank and cross-chain "
              "is wrong.");

// Within one chain the rank is the low nibble, so the order there is the
// declaration order of the chain's members.  Every accelerator scope
// must stay incomparable to every host domain in both directions, with
// System as the join and Thread as the meet of a cross-chain pair.
// Admitting one chain for the other would let a fence that orders
// nothing on the requesting side stand in for one that does.
[[nodiscard]] consteval bool chains_order_within_and_not_across() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            constexpr MemoryScope a = [:ea:];
            constexpr MemoryScope b = [:eb:];
            if constexpr (mem_scope_same_trunk(a, b)) {
                const bool ranked = std::to_underlying(a) <= std::to_underlying(b);
                if (MemoryScopeLattice::leq(a, b) != ranked) return false;
            } else if constexpr ((mem_scope_is_accel(a) && mem_scope_is_arm(b))
                                 || (mem_scope_is_arm(a) && mem_scope_is_accel(b))) {
                if (MemoryScopeLattice::leq(a, b)) return false;
                if (MemoryScopeLattice::join(a, b) != MemoryScope::System) return false;
                if (MemoryScopeLattice::meet(a, b) != MemoryScope::Thread) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(chains_order_within_and_not_across(),
              "The accelerator chain and the host chain must each order by rank and "
              "must stay incomparable to each other, joined only at System and met "
              "only at Thread.");

static_assert(MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Gpu),
              "A device-wide fence must satisfy a block-scope requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::Cta),
              "A block-scope fence is too narrow for a device-wide requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Inner),
              "An outer-shareable fence is wider than an inner-shareable "
              "requirement, not narrower, so the descending direction is false.");

// A bounded order with a single top and bottom and with two unordered internal
// chains cannot be distributive, so the failure below is structural rather than
// a defect.  It is pinned so that anyone merging the two chains has to confront
// the assertion first.
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = MemoryScopeLattice;
    auto lhs = L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer);
    auto rhs = L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer), L::meet(MemoryScope::Inner, MemoryScope::Outer));
    return lhs == MemoryScope::Outer && rhs == MemoryScope::Inner && lhs != rhs;
}
static_assert(non_distributive_witness(), "MemoryScopeLattice must stay non-distributive.  A failure means either "
                                          "the accelerator and host chains were collapsed into one, which "
                                          "destroys their incomparability, or an intermediate element was added "
                                          "that closed the distributivity gap.  Audit before resolving.");

// The shape of every At<scope>, walked by reflection.
static_assert(verify_pinned_at<MemoryScopeLattice>(),
              "MemoryScopeLattice::At<S>: a pinned grade lost its emptiness, its "
              "conversion back to S, or its reflected name.");

static_assert(MemoryScopeLattice::name() == "MemoryScopeLattice");
static_assert(memory_scope::ThreadScope::name() == "MemoryScopeLattice::At<Thread>");
static_assert(memory_scope::SystemScope::name() == "MemoryScopeLattice::At<System>");
static_assert(MemoryScopeLattice::At<static_cast<MemoryScope>(0x30)>::name() == "MemoryScopeLattice::At<?>");

static_assert(memory_scope_name(MemoryScope::Cluster) == "Cluster");
static_assert(memory_scope_name(static_cast<MemoryScope>(0x30)) == "<unknown MemoryScope>");

static_assert(memory_scope::CtaScope::scope == MemoryScope::Cta);
static_assert(memory_scope::SystemScope::scope == MemoryScope::System);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using SystemScopeGraded = Graded<ModalityKind::Absolute, memory_scope::SystemScope, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemScopeGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemScopeGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemScopeGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemScopeGraded, double);

template <typename T_>
using CtaGraded = Graded<ModalityKind::Absolute, memory_scope::CtaScope, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CtaGraded, EightByteValue);

template <typename T_>
using OuterGraded = Graded<ModalityKind::Absolute, memory_scope::OuterScope, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(OuterGraded, EightByteValue);

}  // namespace detail::memory_scope_lattice_self_test

}  // namespace foundation::algebra::lattices
