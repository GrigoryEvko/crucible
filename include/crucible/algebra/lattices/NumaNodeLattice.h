#pragma once

// ── crucible::algebra::lattices::NumaNodeLattice ────────────────────
//
// PARTIAL-ORDER lattice over a NUMA node identifier with two
// sentinel positions: None (bottom) and Any (top).  Specific node
// IDs sit between bottom and top as MUTUALLY INCOMPARABLE siblings
// — same partial-order shape as VendorLattice (FOUND-G53) where
// distinct vendors are siblings and Portable is the wildcard top.
//
// One of two component sub-lattices for the NumaPlacement product
// wrapper from 28_04_2026_effects.md §4.4.3 (FOUND-G71).
//
// Citation: THREADING.md §5.4 (cache-tier rule with NUMA-local
// placement); CRUCIBLE.md §L13 (NUMA-aware ThreadPool placement).
//
// THE LOAD-BEARING USE CASE: AdaptiveScheduler placement decisions.
// A value tagged with NumaNodeId{2} was produced on (or strongly
// prefers to run on) NUMA node 2.  A scheduler asking "can this
// task run at NUMA node N?" admits the value iff the value's
// claim is at least as permissive as the schedule slot:
//
//   leq(scheduler_request, value_claim) — admit iff value's
//                                          placement subsumes the
//                                          schedule slot.
//
// ── The catalog ────────────────────────────────────────────────────
//
// Concrete node IDs occupy values 0..253 (most production systems
// have ≤16 NUMA nodes; Crucible's 8-bit underlying type comfortably
// covers up to 254 specific nodes).  Two sentinel values:
//
//     None = 254   bottom: unbound — value claims no admissible
//                  placement.  Used for sentinel / pre-init state.
//     Any  = 255   top: wildcard — value admits any NUMA node.
//                  Used for NUMA-agnostic data (e.g., constants,
//                  configuration, the Cipher cold-tier).
//
// ── Algebraic shape (partial-order) ────────────────────────────────
//
// Carrier:  NumaNodeId = strong scoped enum : uint8_t.
// Order:    leq(None, anything)            = true (bottom under all)
//           leq(anything, Any)              = true (everything under top)
//           leq(NodeA, NodeB) when A == B   = true (reflexive)
//           leq(NodeA, NodeB) when A != B   = false (siblings)
// Bottom:   NumaNodeId::None
// Top:      NumaNodeId::Any
// Join:     leq-aware least-upper-bound (siblings → top)
// Meet:     leq-aware greatest-lower-bound (siblings → bottom)
//
// ── Direction convention ────────────────────────────────────────────
//
// Wildcard at TOP, sentinel at BOTTOM.  Same as Vendor:
//
//   - Bottom = None = "no admissible placement" = WEAKEST claim
//     (admits nowhere; the WEAKEST possible promise about where
//     this value can be scheduled).
//   - Top = Any = "admits any NUMA node" = STRONGEST claim
//     (the value works everywhere; subsumes every consumer's
//     placement gate).
//
// Specific node IDs are between bottom and top, mutually
// incomparable.  This MATCHES the project chain-wrapper convention
// (bottom = weakest, top = strongest) but with a partial-order
// structure rather than a total chain.  A future maintainer
// expanding this lattice with a NUMA-distance-aware ordering
// (Node A "closer" to Node B than Node C, via SLIT distance
// matrix) should ship a SEPARATE lattice — do NOT mutate the
// partial-order direction here.
//
//   Axiom coverage:
//     TypeSafe — NumaNodeId is a strong scoped enum; mixing with
//                AffinityMask (the sister axis) is a compile error.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = NumaNodeId = 1 byte.  When wrapped in
//     `Graded<Absolute, NumaNodeLattice, T>` directly (without the
//     ProductLattice composition), the grade is 1 byte.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── NumaNodeId — strong scoped enum over NUMA node IDs ─────────────
enum class NumaNodeId : std::uint8_t {
    // 0..253 are CONCRETE node IDs — production NUMA systems
    // typically have 1-16 nodes; the 254-value range is comfortably
    // future-proof.
    None = 254,    // bottom: unbound
    Any  = 255,    // top: wildcard
};

// ── NumaNodeLattice — partial-order with wildcard top ──────────────
struct NumaNodeLattice {
    using element_type = NumaNodeId;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return NumaNodeId::None;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return NumaNodeId::Any;
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == NumaNodeId::None) return true;       // bottom under everything
        if (b == NumaNodeId::Any)  return true;       // everything under top
        return false;                                  // specific != specific siblings
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (leq(a, b)) return b;
        if (leq(b, a)) return a;
        return NumaNodeId::Any;                        // siblings → top
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (leq(a, b)) return a;
        if (leq(b, a)) return b;
        return NumaNodeId::None;                       // siblings → bottom
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "NumaNodeLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::numa_node_lattice_self_test {

static_assert(Lattice<NumaNodeLattice>);
static_assert(BoundedLattice<NumaNodeLattice>);
static_assert(!UnboundedLattice<NumaNodeLattice>);
static_assert(!Semiring<NumaNodeLattice>);

static_assert(sizeof(NumaNodeId) == 1);
static_assert(std::is_trivially_copyable_v<NumaNodeId>);

// Bounds.
static_assert(NumaNodeLattice::bottom() == NumaNodeId::None);
static_assert(NumaNodeLattice::top()    == NumaNodeId::Any);

// Reflexivity.
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::None));
static_assert(NumaNodeLattice::leq(NumaNodeId::Any,  NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{0},     NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId{42},    NumaNodeId{42}));

// Bottom under everything.
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{42}));

// Everything under top.
static_assert(NumaNodeLattice::leq(NumaNodeId{0},  NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::Any));

// Sibling rejection — load-bearing for partial-order discipline.
static_assert(!NumaNodeLattice::leq(NumaNodeId{0},  NumaNodeId{1}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{1},  NumaNodeId{0}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId{43}));

// Join witnesses.
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{0}) == NumaNodeId{0});
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId::None) == NumaNodeId{0});
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId::Any)  == NumaNodeId::Any);
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{1})    == NumaNodeId::Any);
static_assert(NumaNodeLattice::join(NumaNodeId::None, NumaNodeId::Any) == NumaNodeId::Any);

// Meet witnesses.
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{0}) == NumaNodeId{0});
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId::None) == NumaNodeId::None);
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId::Any)  == NumaNodeId{0});
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{1})    == NumaNodeId::None);
static_assert(NumaNodeLattice::meet(NumaNodeId::None, NumaNodeId::Any) == NumaNodeId::None);

// Idempotence.
static_assert(NumaNodeLattice::join(NumaNodeId{42}, NumaNodeId{42}) == NumaNodeId{42});
static_assert(NumaNodeLattice::meet(NumaNodeId{42}, NumaNodeId{42}) == NumaNodeId{42});

// Bound identities.
static_assert(NumaNodeLattice::join(NumaNodeId{42}, NumaNodeLattice::bottom()) == NumaNodeId{42});
static_assert(NumaNodeLattice::meet(NumaNodeId{42}, NumaNodeLattice::top())    == NumaNodeId{42});

// Antisymmetry — siblings reject in both directions.
static_assert(!NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{1})
           && !NumaNodeLattice::leq(NumaNodeId{1}, NumaNodeId{0}));

inline void runtime_smoke_test() {
    NumaNodeId                bot   = NumaNodeLattice::bottom();
    NumaNodeId                topv  = NumaNodeLattice::top();
    NumaNodeId                node2 {2};
    [[maybe_unused]] bool     l     = NumaNodeLattice::leq(bot, topv);
    [[maybe_unused]] NumaNodeId j   = NumaNodeLattice::join(node2, topv);
    [[maybe_unused]] NumaNodeId m   = NumaNodeLattice::meet(node2, bot);

    // Sibling join → top (wildcard).
    NumaNodeId joined_siblings =
        NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{1});
    if (joined_siblings != NumaNodeId::Any) std::abort();

    // Sibling meet → bottom (none).
    NumaNodeId meet_siblings =
        NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{1});
    if (meet_siblings != NumaNodeId::None) std::abort();

    // Lattice over Graded substrate.
    using NumaNodeGraded = Graded<ModalityKind::Absolute, NumaNodeLattice, int>;
    NumaNodeGraded            v{42, NumaNodeId{2}};
    [[maybe_unused]] auto     g  = v.grade();
    [[maybe_unused]] auto     vp = v.peek();
}

}  // namespace detail::numa_node_lattice_self_test

}  // namespace crucible::algebra::lattices
