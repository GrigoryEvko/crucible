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

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

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

inline constexpr std::size_t memory_scope_count = std::meta::enumerators_of(^^MemoryScope).size();

[[nodiscard]] consteval std::string_view memory_scope_name(MemoryScope x) noexcept {
    switch (x) {
        case MemoryScope::Thread:
            return "Thread";
        case MemoryScope::Warp:
            return "Warp";
        case MemoryScope::Cta:
            return "Cta";
        case MemoryScope::Cluster:
            return "Cluster";
        case MemoryScope::Gpu:
            return "Gpu";
        case MemoryScope::Inner:
            return "Inner";
        case MemoryScope::Outer:
            return "Outer";
        case MemoryScope::System:
            return "System";
        default:
            return std::string_view{"<unknown MemoryScope>"};
    }
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
    struct At {
        struct element_type {
            using memory_scope_value_type = MemoryScope;
            [[nodiscard]] constexpr operator memory_scope_value_type() const noexcept { return S; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr MemoryScope scope = S;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (S) {
                case MemoryScope::Thread:
                    return "MemoryScopeLattice::At<Thread>";
                case MemoryScope::Warp:
                    return "MemoryScopeLattice::At<Warp>";
                case MemoryScope::Cta:
                    return "MemoryScopeLattice::At<Cta>";
                case MemoryScope::Cluster:
                    return "MemoryScopeLattice::At<Cluster>";
                case MemoryScope::Gpu:
                    return "MemoryScopeLattice::At<Gpu>";
                case MemoryScope::Inner:
                    return "MemoryScopeLattice::At<Inner>";
                case MemoryScope::Outer:
                    return "MemoryScopeLattice::At<Outer>";
                case MemoryScope::System:
                    return "MemoryScopeLattice::At<System>";
                default:
                    return "MemoryScopeLattice::At<?>";
            }
        }
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
                                       "underlying value, and kAll in the verifier below.");

[[nodiscard]] consteval bool every_memory_scope_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (memory_scope_name([:en:]) == std::string_view{"<unknown MemoryScope>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_memory_scope_has_name(), "memory_scope_name() switch missing an arm for at least one scope.");

static_assert(Lattice<MemoryScopeLattice>);
static_assert(BoundedLattice<MemoryScopeLattice>);
static_assert(Lattice<memory_scope::ThreadScope>);
static_assert(Lattice<memory_scope::CtaScope>);
static_assert(Lattice<memory_scope::OuterScope>);
static_assert(Lattice<memory_scope::SystemScope>);
static_assert(BoundedLattice<memory_scope::SystemScope>);

static_assert(!UnboundedLattice<MemoryScopeLattice>);
static_assert(!Semiring<MemoryScopeLattice>);

static_assert(std::is_empty_v<memory_scope::ThreadScope::element_type>);
static_assert(std::is_empty_v<memory_scope::CtaScope::element_type>);
static_assert(std::is_empty_v<memory_scope::OuterScope::element_type>);
static_assert(std::is_empty_v<memory_scope::SystemScope::element_type>);

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

static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Thread));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Cta));
static_assert(MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Outer));
static_assert(MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::System));

static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Warp));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Gpu));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Inner));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Outer));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::System));

static_assert(MemoryScopeLattice::leq(MemoryScope::Warp, MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::System));

static_assert(MemoryScopeLattice::leq(MemoryScope::Warp, MemoryScope::Cta));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Cluster));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cluster, MemoryScope::Gpu));
static_assert(MemoryScopeLattice::leq(MemoryScope::Warp, MemoryScope::Gpu));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Gpu),
              "A device-wide fence must satisfy a block-scope requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::Cta),
              "A block-scope fence is too narrow for a device-wide requirement.");

static_assert(MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Inner),
              "An outer-shareable fence is wider than an inner-shareable "
              "requirement, not narrower, so the descending direction is false.");

// Every accelerator scope must stay incomparable to every host domain in both
// directions.  Admitting one for the other would let a fence that orders
// nothing on the requesting side stand in for one that does.
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Inner));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::Cta));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Gpu));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Warp, MemoryScope::Inner));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::Warp));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cluster, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Cluster));

static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Cta));
static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Thread));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Thread));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Thread));

static_assert(MemoryScopeLattice::join(MemoryScope::Warp, MemoryScope::Gpu) == MemoryScope::Gpu);
static_assert(MemoryScopeLattice::meet(MemoryScope::Warp, MemoryScope::Gpu) == MemoryScope::Warp);
static_assert(MemoryScopeLattice::join(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Outer);
static_assert(MemoryScopeLattice::meet(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Inner);
static_assert(MemoryScopeLattice::join(MemoryScope::Cta, MemoryScope::Inner) == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::Cta, MemoryScope::Inner) == MemoryScope::Thread);
static_assert(MemoryScopeLattice::join(MemoryScope::Gpu, MemoryScope::Outer) == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::Gpu, MemoryScope::Outer) == MemoryScope::Thread);
static_assert(MemoryScopeLattice::join(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Cta);
static_assert(MemoryScopeLattice::join(MemoryScope::Thread, MemoryScope::System) == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::System, MemoryScope::Outer) == MemoryScope::Outer);
static_assert(MemoryScopeLattice::meet(MemoryScope::System, MemoryScope::Thread) == MemoryScope::Thread);
static_assert(MemoryScopeLattice::meet(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Thread);
static_assert(MemoryScopeLattice::join(MemoryScope::System, MemoryScope::Inner) == MemoryScope::System);
static_assert(MemoryScopeLattice::join(MemoryScope::Cta, MemoryScope::Cta) == MemoryScope::Cta);
static_assert(MemoryScopeLattice::meet(MemoryScope::Outer, MemoryScope::Outer) == MemoryScope::Outer);

// The shared chain verifier does not apply to a partial order, so the axioms
// are walked by hand over every triple of the eight elements.
inline constexpr MemoryScope kAll[] = {
    MemoryScope::Thread, MemoryScope::Warp,  MemoryScope::Cta,   MemoryScope::Cluster,
    MemoryScope::Gpu,    MemoryScope::Inner, MemoryScope::Outer, MemoryScope::System,
};

[[nodiscard]] consteval bool verify_partial_order_exhaustive() noexcept {
    using L = MemoryScopeLattice;
    for (auto a : kAll) {
        if (!L::leq(a, a)) return false;
        for (auto b : kAll) {
            if (L::leq(a, b) && L::leq(b, a) && a != b) return false;
            if (L::join(a, b) != L::join(b, a)) return false;
            if (L::meet(a, b) != L::meet(b, a)) return false;
            if (L::join(a, a) != a) return false;
            if (L::meet(a, a) != a) return false;
            if (L::join(a, L::meet(a, b)) != a) return false;
            if (L::meet(a, L::join(a, b)) != a) return false;
            if (!L::leq(L::bottom(), a)) return false;
            if (!L::leq(a, L::top())) return false;
            for (auto c : kAll) {
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
                bool by_meet = (L::meet(a, b) == a);
                bool by_join = (L::join(a, b) == b);
                bool by_leq = L::leq(a, b);
                if (by_leq != by_meet) return false;
                if (by_leq != by_join) return false;
            }
        }
    }
    return true;
}
static_assert(verify_partial_order_exhaustive(),
              "The partial-order axioms must hold at every triple of the eight "
              "scopes.  A failure means leq, join or meet is wrong for some pair, or "
              "the routing between Thread, System, same-chain rank and cross-chain "
              "is wrong.");

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

static_assert(MemoryScopeLattice::name() == "MemoryScopeLattice");
static_assert(memory_scope::ThreadScope::name() == "MemoryScopeLattice::At<Thread>");
static_assert(memory_scope::CtaScope::name() == "MemoryScopeLattice::At<Cta>");
static_assert(memory_scope::GpuScope::name() == "MemoryScopeLattice::At<Gpu>");
static_assert(memory_scope::InnerScope::name() == "MemoryScopeLattice::At<Inner>");
static_assert(memory_scope::OuterScope::name() == "MemoryScopeLattice::At<Outer>");
static_assert(memory_scope::SystemScope::name() == "MemoryScopeLattice::At<System>");

[[nodiscard]] consteval bool every_at_memory_scope_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (MemoryScopeLattice::At<([:en:])>::name() == std::string_view{"MemoryScopeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_memory_scope_has_name(), "MemoryScopeLattice::At<S>::name() switch missing an arm.");

static_assert(memory_scope::CtaScope::scope == MemoryScope::Cta);
static_assert(memory_scope::OuterScope::scope == MemoryScope::Outer);
static_assert(memory_scope::ThreadScope::scope == MemoryScope::Thread);
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

inline void runtime_smoke_test() {
    MemoryScope a = MemoryScope::Cta;
    MemoryScope b = MemoryScope::Inner;
    [[maybe_unused]] bool l1 = MemoryScopeLattice::leq(a, b);
    [[maybe_unused]] MemoryScope j1 = MemoryScopeLattice::join(a, b);
    [[maybe_unused]] MemoryScope m1 = MemoryScopeLattice::meet(a, b);
    [[maybe_unused]] MemoryScope bot = MemoryScopeLattice::bottom();
    [[maybe_unused]] MemoryScope topv = MemoryScopeLattice::top();

    MemoryScope warp = MemoryScope::Warp;
    [[maybe_unused]] bool within = MemoryScopeLattice::leq(warp, a);  // Warp ⊑ Cta
    [[maybe_unused]] bool xtrunk = mem_scope_same_trunk(a, b);  // false

    OneByteValue v{42};
    SystemScopeGraded<OneByteValue> initial{v, memory_scope::SystemScope::bottom()};
    auto widened = initial.weaken(memory_scope::SystemScope::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto g = widened.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    memory_scope::SystemScope::element_type e{};
    [[maybe_unused]] MemoryScope rec = e;
}

}  // namespace detail::memory_scope_lattice_self_test

}  // namespace crucible::algebra::lattices
