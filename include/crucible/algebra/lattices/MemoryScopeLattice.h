#pragma once

// ── crucible::algebra::lattices::MemoryScopeLattice ─────────────────
//
// NON-DISTRIBUTIVE partial-order lattice over the MEMORY VISIBILITY /
// COHERENCE-DOMAIN spectrum — the SECOND coordinate (with BarrierStrength,
// FIXY-V-252) of every accelerator + ARM-host memory model.  Two MUTUALLY-
// INCOMPARABLE internal chains — the accelerator (GPU) trunk and the ARM
// shareability trunk — joined only at a shared bottom (Thread, narrowest
// visibility) and a shared top (System, full-system visibility).  The
// grading axis underlying the ScopedFence wrapper (FIXY-V-267).
//
// ── Structural shape (two chains sharing ⊤ and ⊥) ──────────────────
//
//                          ┌──────────────┐
//                          │  System  ⊤   │  widest visibility
//                          └──────┬───────┘  (host/peer/NIC — .sys / DMB SY)
//                  ┌──────────────┴──────────────┐
//                  │                              │
//             ┌────┴─────┐                  ┌─────┴────┐
//             │   Gpu    │ (accel top)      │  Outer   │ (ARM top — OSH)
//             │ Cluster  │                  │  Inner   │ (ARM bottom — ISH)
//             │   Cta    │                  └─────┬────┘
//             │   Warp   │ (accel bottom)         │
//             └────┬─────┘                        │
//                  └──────────────┬───────────────┘
//                                 ▼
//                          ┌──────────────┐
//                          │  Thread  ⊥   │  narrowest visibility
//                          └──────────────┘  (issuing thread only)
//
// Each trunk is an INTERNAL CHAIN (totally ordered: Warp ⊑ Cta ⊑ Cluster
// ⊑ Gpu; Inner ⊑ Outer).  Across trunks every pair is INCOMPARABLE — a PTX
// `.cta` fence has NO ordering relation to an ARM `DMB ISH`; neither
// subsumes the other.  Like SimdIsaLattice (V-250) and VendorLattice, this
// is a hand-written partial order, NOT a ChainLattice.
//
// ── On System (⊤) = PTX `.sys` = ARM `DMB SY` (the shared top) ─────
//
// Both GPU `.sys` scope and ARM full-system `DMB SY` denote ONE concept:
// the value is visible to ALL observers (host, peers, DMA, NIC).  There is
// no meaningful "GPU full-system" vs "ARM full-system" distinction once a
// value has full-system visibility — the whole machine sees it.  So the
// two trunks CONVERGE at a single shared ⊤ (System), exactly as SimdIsa's
// two trunks converge at Portable.  The difference from SimdIsa: SimdIsa's
// ⊤ (Portable, a run-anywhere kernel) is an abstraction with no single
// instruction; MemoryScope's ⊤ has CONCRETE per-arch realizations (the
// V-272 lower_fence table maps (System, NV) → `fence.sc.sys` and
// (System, ARM) → `DMB SY`).  The trunk INTERIORS stay incomparable: Cta
// (GPU) ⋢ Inner (ARM) in both directions — that is the load-bearing fact.
//
// ── On the abstract VISIBILITY ladder (Thread / Warp not PTX scopes) ─
//
// PTX realizes only `.cta / .cluster / .gpu / .sys` as `.scope` qualifiers.
// Thread (issuing-thread-only, e.g. a relaxed atomic on a thread's own
// variable — CLAUDE.md §IX) and Warp (warp-coherent, realized by warp-sync
// primitives, not a `.scope` token) are narrower VISIBILITY levels the
// lattice models so the optimizer can prove "no cross-thread fence needed
// here".  The V-272 lower_fence table maps Thread → no fence and Warp →
// warp-sync/none; Cta/Cluster/Gpu → the `.scope`-qualified fence; System →
// `.sys` / `DMB SY`.  The lattice models VISIBILITY; the lowering picks the
// cheapest realization.
//
// THE LOAD-BEARING USE CASE (consumed by V-267 ScopedFence::satisfies<> +
// V-268 collision rules V401/V402): a publication declared
// `requires ScopedFence<Cta>::satisfies<RuntimeFloor>` is satisfied by a
// device-wide (Gpu) or system (System) fence (Cta ⊑ Gpu ⊑ System) but
// REJECTS a warp-only fence (Cta ⋢ Warp) AND rejects every ARM-domain fence
// (cross-trunk incomparable).  The `satisfies` wrapper is V-267's
// deliverable; this header ships only the lattice (leq / join / meet /
// At<>), mirroring the SimdIsaLattice → safety/SimdWidthPinned.h split.
//
// ── Direction convention (matches the audit-verified universal) ────
//
// Wider visibility = HIGHER in the lattice.  `leq(required, provided)`
// reads "a value requiring visibility scope `required` is satisfied by a
// fence that publishes at scope `provided`" — i.e. the required scope must
// be at-or-below the provided fence's scope.
//
//   leq(Cta, Gpu)      = true  (within accel trunk; a device fence covers a block requirement)
//   leq(Gpu, Cta)      = false (a block-scope fence is too narrow for a device requirement)
//   leq(Cta, Inner)    = false (cross-trunk: a GPU block scope vs an ARM ISH domain)
//   leq(Thread, Cta)   = true  (⊥: thread-local is the weakest requirement, satisfied anywhere)
//   leq(Gpu, System)   = true  (⊤: full-system visibility satisfies every requirement)
//   leq(System, Gpu)   = false (a device fence does not provide full-system visibility)
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class MemoryScope.  Underlying encoding packs the trunk
// into the high nibble (accel = 0x1_, ARM = 0x2_) with the per-trunk
// visibility rank in the low nibble, so the integer order WITHIN a trunk
// equals scope width.  Thread = 0x00 (⊥), System = 0xFF (⊤).  The
// underlying-integer order is NOT the lattice order across trunks —
// MemoryScope is a partial order, not a chain.
//
// Bottom = Thread  (narrowest — issuing thread only; satisfies no cross-thread gate)
// Top    = System  (widest — full-system visibility; satisfies every gate)
// Join (LUB):  same trunk → wider (higher rank); distinct trunks → System (⊤).
// Meet (GLB):  same trunk → narrower (lower rank); distinct trunks → Thread (⊥).
//
// NOT distributive — verified below.  Like SimdIsaLattice, a partial order
// bounded by a single ⊤/⊥ with two incomparable internal chains CANNOT be
// distributive.  The canonical failure (operands in different trunks, with
// one trunk-internal pair related):
//   (Gpu ∨ Inner) ∧ Outer = System ∧ Outer = Outer
//   (Gpu ∧ Outer) ∨ (Inner ∧ Outer) = Thread ∨ Inner = Inner   (Inner ⊑ Outer in ARM trunk)
//   Outer ≠ Inner — hence non-distributive.
//
//   Axiom coverage:
//     TypeSafe — MemoryScope is a strong scoped enum; cross-tier mixing
//                requires `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — a handful of integer compares over an 8-element
//     domain; the carrier compiles to a 1-byte field.  Type-pinned via
//     `MemoryScopeLattice::At<Scope>`, the grade EBO-collapses to zero
//     bytes.
//
// See FIXY-V-250 (SimdIsaLattice) for the sibling two-trunk partial order;
// FIXY-V-252 (BarrierStrengthLattice) for the strength axis this composes
// with; FIXY-V-267 (safety/ScopedFence.h) for the type-pinned wrapper that
// ships `satisfies<>` + the `row_hash_contribution<ScopedFence<...>>`
// federation-cache discriminator (deferred to the wrapper exactly as
// SimdIsaLattice defers to safety/SimdWidthPinned.h — the lattice layer
// pulls no safety/diag header).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── MemoryScope — partial order over memory-visibility scope ────────
//
// Encoding: high nibble = trunk (1 = accel/GPU, 2 = ARM), low nibble =
// visibility rank.  Thread = 0x00 (⊥), System = 0xFF (⊤).  Within a trunk
// the integer order IS the scope-width rank; across trunks the integer
// order is meaningless (the lattice is a partial order).
enum class MemoryScope : std::uint8_t {
    Thread   = 0x00,  // ⊥: visible only to the issuing thread; satisfies no cross-thread gate
    // Accelerator (GPU) trunk (narrow → wide)
    Warp     = 0x10,  // warp-coherent (warp-sync primitives; no `.scope` token)
    Cta      = 0x11,  // thread-block / cooperative-thread-array (PTX `.cta`)
    Cluster  = 0x12,  // thread-block cluster (Hopper, PTX `.cluster`)
    Gpu      = 0x13,  // device-wide (PTX `.gpu`)
    // ARM-host shareability trunk (narrow → wide)
    Inner    = 0x20,  // inner-shareable domain (DMB ISH)
    Outer    = 0x21,  // outer-shareable domain (DMB OSH)
    System   = 0xFF,  // ⊤: full-system visibility (PTX `.sys` / ARM `DMB SY`)
};

inline constexpr std::size_t memory_scope_count =
    std::meta::enumerators_of(^^MemoryScope).size();

[[nodiscard]] consteval std::string_view memory_scope_name(MemoryScope x) noexcept {
    switch (x) {
        case MemoryScope::Thread:  return "Thread";
        case MemoryScope::Warp:    return "Warp";
        case MemoryScope::Cta:     return "Cta";
        case MemoryScope::Cluster: return "Cluster";
        case MemoryScope::Gpu:     return "Gpu";
        case MemoryScope::Inner:   return "Inner";
        case MemoryScope::Outer:   return "Outer";
        case MemoryScope::System:  return "System";
        default:                   return std::string_view{"<unknown MemoryScope>"};
    }
}

// ── Trunk classification (the partial-order discriminator) ─────────
[[nodiscard]] constexpr bool mem_scope_is_accel(MemoryScope x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(MemoryScope::Warp) &&
           u <= std::to_underlying(MemoryScope::Gpu);
}
[[nodiscard]] constexpr bool mem_scope_is_arm(MemoryScope x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(MemoryScope::Inner) &&
           u <= std::to_underlying(MemoryScope::Outer);
}
// Two scopes share a trunk iff both are accel-trunk or both are ARM-trunk.
// Thread and System belong to NEITHER trunk (they are the shared
// sentinels), so same_trunk(Thread, x) and same_trunk(System, x) are
// always false — leq/join/meet special-case them before this check.
[[nodiscard]] constexpr bool mem_scope_same_trunk(MemoryScope a, MemoryScope b) noexcept {
    return (mem_scope_is_accel(a) && mem_scope_is_accel(b)) ||
           (mem_scope_is_arm(a) && mem_scope_is_arm(b));
}

// ── Full MemoryScopeLattice (partial order) ─────────────────────────
struct MemoryScopeLattice {
    using element_type = MemoryScope;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return MemoryScope::Thread;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return MemoryScope::System;
    }

    // leq(a, b) — partial-order check.  Priority order:
    //   1. Reflexive: x ⊑ x.
    //   2. Bottom: Thread ⊑ x for every x.
    //   3. Top: x ⊑ System for every x.
    //   4. Same trunk: compare per-trunk rank (the low-nibble integer).
    //   5. Otherwise (cross-trunk) incomparable.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == MemoryScope::Thread) return true;
        if (b == MemoryScope::System) return true;
        if (mem_scope_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b);
        }
        return false;
    }

    // join(a, b) — least upper bound.  Cross-trunk pairs route through ⊤.
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
        return MemoryScope::System;  // distinct trunks: LUB is ⊤
    }

    // meet(a, b) — greatest lower bound.  Cross-trunk pairs route through ⊥.
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
        return MemoryScope::Thread;  // distinct trunks: GLB is ⊥
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "MemoryScopeLattice";
    }

    // ── At<Scope> — per-scope singleton sub-lattice ────────────────
    //
    // Mirrors SimdIsaLattice::At<I>.  Empty element_type for regime-1 EBO
    // collapse; within a single fixed scope there is one element, so leq is
    // trivially true and join/meet return the singleton.  The full
    // MemoryScopeLattice partial order is consulted when the dispatcher
    // checks `satisfies<>` between two distinct fixed-scope wrappers
    // (V-267).
    template <MemoryScope S>
    struct At {
        struct element_type {
            using memory_scope_value_type = MemoryScope;
            [[nodiscard]] constexpr operator memory_scope_value_type() const noexcept {
                return S;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr MemoryScope scope = S;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (S) {
                case MemoryScope::Thread:  return "MemoryScopeLattice::At<Thread>";
                case MemoryScope::Warp:    return "MemoryScopeLattice::At<Warp>";
                case MemoryScope::Cta:     return "MemoryScopeLattice::At<Cta>";
                case MemoryScope::Cluster: return "MemoryScopeLattice::At<Cluster>";
                case MemoryScope::Gpu:     return "MemoryScopeLattice::At<Gpu>";
                case MemoryScope::Inner:   return "MemoryScopeLattice::At<Inner>";
                case MemoryScope::Outer:   return "MemoryScopeLattice::At<Outer>";
                case MemoryScope::System:  return "MemoryScopeLattice::At<System>";
                default:                   return "MemoryScopeLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace memory_scope {
    using ThreadScope  = MemoryScopeLattice::At<MemoryScope::Thread>;
    using WarpScope    = MemoryScopeLattice::At<MemoryScope::Warp>;
    using CtaScope     = MemoryScopeLattice::At<MemoryScope::Cta>;
    using ClusterScope = MemoryScopeLattice::At<MemoryScope::Cluster>;
    using GpuScope     = MemoryScopeLattice::At<MemoryScope::Gpu>;
    using InnerScope   = MemoryScopeLattice::At<MemoryScope::Inner>;
    using OuterScope   = MemoryScopeLattice::At<MemoryScope::Outer>;
    using SystemScope  = MemoryScopeLattice::At<MemoryScope::System>;
}  // namespace memory_scope

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::memory_scope_lattice_self_test {

static_assert(memory_scope_count == 8,
    "MemoryScope catalog diverged from {Thread, 4 accel trunk (Warp, Cta, "
    "Cluster, Gpu), 2 ARM trunk (Inner, Outer), System}; confirm intent and "
    "update the trunk-classification helpers (mem_scope_is_accel / "
    "mem_scope_is_arm bound the trunk ranges) AND the V-267 ScopedFence "
    "satisfies<> + V-268 collision rules AND kAll[] in the verifier below.");

[[nodiscard]] consteval bool every_memory_scope_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
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
static_assert(every_memory_scope_has_name(),
    "memory_scope_name() switch missing an arm for at least one scope.");

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

// ── Bottom + top witnesses ────────────────────────────────────────
static_assert(MemoryScopeLattice::bottom() == MemoryScope::Thread);
static_assert(MemoryScopeLattice::top()    == MemoryScope::System);

// ── Trunk-classification witnesses ────────────────────────────────
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

// ── Reflexivity at every scope ────────────────────────────────────
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Thread));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta,    MemoryScope::Cta));
static_assert(MemoryScopeLattice::leq(MemoryScope::Outer,  MemoryScope::Outer));
static_assert(MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::System));

// ── Thread ⊑ everything ───────────────────────────────────────────
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Warp));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Gpu));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Inner));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::Outer));
static_assert(MemoryScopeLattice::leq(MemoryScope::Thread, MemoryScope::System));

// ── everything ⊑ System ───────────────────────────────────────────
static_assert(MemoryScopeLattice::leq(MemoryScope::Warp,  MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Gpu,   MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::System));
static_assert(MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::System));

// ── Accel trunk subsumption chain (the LOAD-BEARING positive) ─────
// Warp ⊑ Cta ⊑ Cluster ⊑ Gpu
static_assert(MemoryScopeLattice::leq(MemoryScope::Warp,    MemoryScope::Cta));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta,     MemoryScope::Cluster));
static_assert(MemoryScopeLattice::leq(MemoryScope::Cluster, MemoryScope::Gpu));
static_assert(MemoryScopeLattice::leq(MemoryScope::Warp,    MemoryScope::Gpu));   // transitive
// Use-case witness: a device fence satisfies a block requirement; a warp
// fence does not.
static_assert(MemoryScopeLattice::leq(MemoryScope::Cta,  MemoryScope::Gpu),
    "FIXY-V-265: Cta ⊑ Gpu — a device-wide fence satisfies a block-scope "
    "requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::Cta),
    "FIXY-V-265: Gpu ⋢ Cta — a block-scope fence is too narrow for a "
    "device-wide requirement.");

// ── ARM trunk subsumption chain (the LOAD-BEARING positive) ───────
// Inner (ISH) ⊑ Outer (OSH)
static_assert(MemoryScopeLattice::leq(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Inner),
    "FIXY-V-265: Outer ⋢ Inner — an outer-shareable fence is wider than "
    "an inner-shareable requirement, not narrower; descending is false.");

// ── Cross-trunk incomparability (THE LOAD-BEARING NEGATIVE) ───────
// Every accel × ARM pair MUST be mutually incomparable — this is what the
// V-267 ScopedFence safety guarantee depends on (a GPU-scope fence must
// never be admitted for an ARM-domain requirement and vice versa).
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cta,     MemoryScope::Inner));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Inner,   MemoryScope::Cta));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu,     MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer,   MemoryScope::Gpu));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Warp,    MemoryScope::Inner));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Inner,   MemoryScope::Warp));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cluster, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer,   MemoryScope::Cluster));

// ── Reverse rules — System ⊑ X false, X ⊑ Thread false ────────────
static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Cta));
static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Outer));
static_assert(!MemoryScopeLattice::leq(MemoryScope::System, MemoryScope::Thread));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Cta,    MemoryScope::Thread));
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer,  MemoryScope::Thread));

// ── Join / meet witnesses ─────────────────────────────────────────
// Same trunk → join = wider rank, meet = narrower rank.
static_assert(MemoryScopeLattice::join(MemoryScope::Warp, MemoryScope::Gpu)   == MemoryScope::Gpu);
static_assert(MemoryScopeLattice::meet(MemoryScope::Warp, MemoryScope::Gpu)   == MemoryScope::Warp);
static_assert(MemoryScopeLattice::join(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Outer);
static_assert(MemoryScopeLattice::meet(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Inner);
// Cross-trunk → join = System, meet = Thread.
static_assert(MemoryScopeLattice::join(MemoryScope::Cta, MemoryScope::Inner)  == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::Cta, MemoryScope::Inner)  == MemoryScope::Thread);
static_assert(MemoryScopeLattice::join(MemoryScope::Gpu, MemoryScope::Outer)  == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::Gpu, MemoryScope::Outer)  == MemoryScope::Thread);
// Thread identity for join; System identity for meet.
static_assert(MemoryScopeLattice::join(MemoryScope::Thread, MemoryScope::Cta)    == MemoryScope::Cta);
static_assert(MemoryScopeLattice::join(MemoryScope::Thread, MemoryScope::System) == MemoryScope::System);
static_assert(MemoryScopeLattice::meet(MemoryScope::System, MemoryScope::Outer)  == MemoryScope::Outer);
static_assert(MemoryScopeLattice::meet(MemoryScope::System, MemoryScope::Thread) == MemoryScope::Thread);
// Thread absorbs in meet; System absorbs in join.
static_assert(MemoryScopeLattice::meet(MemoryScope::Thread, MemoryScope::Cta)   == MemoryScope::Thread);
static_assert(MemoryScopeLattice::join(MemoryScope::System, MemoryScope::Inner) == MemoryScope::System);
// Idempotence.
static_assert(MemoryScopeLattice::join(MemoryScope::Cta,   MemoryScope::Cta)   == MemoryScope::Cta);
static_assert(MemoryScopeLattice::meet(MemoryScope::Outer, MemoryScope::Outer) == MemoryScope::Outer);

// ── Exhaustive lattice-axiom verification — (8 scopes)³ = 512 ──────
//
// Hand-written exhaustive verifier (cannot reuse the chain verifier —
// MemoryScope is NOT a chain).  Mirrors SimdIsaLattice's: reflexivity /
// antisymmetry / transitivity of leq + idempotence / commutativity /
// associativity / absorption / bounds of meet+join + leq-consistency.
inline constexpr MemoryScope kAll[] = {
    MemoryScope::Thread,
    MemoryScope::Warp, MemoryScope::Cta, MemoryScope::Cluster, MemoryScope::Gpu,
    MemoryScope::Inner, MemoryScope::Outer,
    MemoryScope::System,
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
            if (!L::leq(a, L::top()))    return false;
            for (auto c : kAll) {
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
                bool by_meet = (L::meet(a, b) == a);
                bool by_join = (L::join(a, b) == b);
                bool by_leq  = L::leq(a, b);
                if (by_leq != by_meet) return false;
                if (by_leq != by_join) return false;
            }
        }
    }
    return true;
}
static_assert(verify_partial_order_exhaustive(),
    "MemoryScopeLattice's partial-order axioms must hold at every "
    "(MemoryScope)³ triple over the 8 elements.  If this fires, one of leq "
    "/ join / meet has a bug for some pair OR the trunk routing (Thread / "
    "System / same-trunk-rank / cross-trunk) is wrong.");

// ── Non-distributivity witness ────────────────────────────────────
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = MemoryScopeLattice;
    // (Gpu ∨ Inner) ∧ Outer = System ∧ Outer = Outer
    // (Gpu ∧ Outer) ∨ (Inner ∧ Outer) = Thread ∨ Inner = Inner   (Inner ⊑ Outer)
    // Outer ≠ Inner — hence non-distributive.
    auto lhs = L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer);
    auto rhs = L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer),
                       L::meet(MemoryScope::Inner, MemoryScope::Outer));
    return lhs == MemoryScope::Outer && rhs == MemoryScope::Inner && lhs != rhs;
}
static_assert(non_distributive_witness(),
    "MemoryScopeLattice MUST be non-distributive (see docblock).  If this "
    "fires, either (a) the accel and ARM trunks were collapsed into one "
    "chain — DEFEATING the cross-trunk incomparability the V-267 ScopedFence "
    "safety guarantee depends on — or (b) a synthetic intermediate element "
    "closed the distributivity gap.  Audit before resolving.");

// ── Names ────────────────────────────────────────────────────────
static_assert(MemoryScopeLattice::name() == "MemoryScopeLattice");
static_assert(memory_scope::ThreadScope::name() == "MemoryScopeLattice::At<Thread>");
static_assert(memory_scope::CtaScope::name()    == "MemoryScopeLattice::At<Cta>");
static_assert(memory_scope::GpuScope::name()    == "MemoryScopeLattice::At<Gpu>");
static_assert(memory_scope::InnerScope::name()  == "MemoryScopeLattice::At<Inner>");
static_assert(memory_scope::OuterScope::name()  == "MemoryScopeLattice::At<Outer>");
static_assert(memory_scope::SystemScope::name() == "MemoryScopeLattice::At<System>");

[[nodiscard]] consteval bool every_at_memory_scope_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (MemoryScopeLattice::At<([:en:])>::name() ==
            std::string_view{"MemoryScopeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_memory_scope_has_name(),
    "MemoryScopeLattice::At<S>::name() switch missing an arm.");

static_assert(memory_scope::CtaScope::scope    == MemoryScope::Cta);
static_assert(memory_scope::OuterScope::scope   == MemoryScope::Outer);
static_assert(memory_scope::ThreadScope::scope  == MemoryScope::Thread);
static_assert(memory_scope::SystemScope::scope  == MemoryScope::System);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using SystemScopeGraded = Graded<ModalityKind::Absolute,
                                 memory_scope::SystemScope, T_>;
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
    [[maybe_unused]] bool        l1   = MemoryScopeLattice::leq(a, b);
    [[maybe_unused]] MemoryScope j1   = MemoryScopeLattice::join(a, b);
    [[maybe_unused]] MemoryScope m1   = MemoryScopeLattice::meet(a, b);
    [[maybe_unused]] MemoryScope bot  = MemoryScopeLattice::bottom();
    [[maybe_unused]] MemoryScope topv = MemoryScopeLattice::top();

    MemoryScope warp = MemoryScope::Warp;
    [[maybe_unused]] bool within = MemoryScopeLattice::leq(warp, a);  // Warp ⊑ Cta
    [[maybe_unused]] bool xtrunk = mem_scope_same_trunk(a, b);        // false

    OneByteValue v{42};
    SystemScopeGraded<OneByteValue> initial{v, memory_scope::SystemScope::bottom()};
    auto widened  = initial.weaken(memory_scope::SystemScope::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto g  = widened.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    memory_scope::SystemScope::element_type e{};
    [[maybe_unused]] MemoryScope rec = e;
}

}  // namespace detail::memory_scope_lattice_self_test

}  // namespace crucible::algebra::lattices
