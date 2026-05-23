// FIXY-V-265 sentinel TU: algebra/lattices/MemoryScopeLattice.h —
// non-distributive partial-order lattice over the memory VISIBILITY /
// coherence-domain spectrum (accelerator/GPU trunk × ARM shareability
// trunk joined only at Thread/System).
//
// V-265 ships ONLY the lattice (the algebraic substrate): the enum +
// leq/join/meet + At<Scope> singletons + trunk-classification helpers +
// reflection-driven cardinality/name-coverage + exhaustive (8³ = 512)
// lattice-axiom verification + the non-distributivity witness.  It ships
// NO value-level machinery and NO DimensionAxis enumerator:
//   - FIXY-V-266 ships DimensionAxis::MemoryScope (the axis enumerator) — so
//     this TU does NOT reference safety/DimensionTraits.h (the axis does
//     not exist yet; referencing it would not compile).
//   - FIXY-V-267 ships safety/ScopedFence.h (the Graded<Absolute,
//     MemoryScopeLattice::At<S>, P> carrier) PLUS the `satisfies<>` gate AND
//     the row_hash_contribution<ScopedFence<...>> federation-cache
//     discriminator — exactly mirroring how SimdIsaLattice defers its
//     wrapper + row_hash to safety/SimdWidthPinned.h + safety/diag/
//     RowHashFold.h.  The lattice layer pulls NO safety/diag header.
//   - FIXY-V-268 ships the V401/V402 CollisionCatalog rules that consume
//     the cross-trunk incomparability witnessed below; FIXY-V-272 ships the
//     mimic lower_fence table keyed on (scope × strength × arch).
//
// Why a dedicated MemoryScope axis (not folded onto BarrierStrength):
// BarrierStrength (V-252) records the ORDERING strength a fence provides
// (None ⊑ ... ⊑ FullFence).  It has no notion of HOW FAR a publication is
// visible.  A `.cta`-scope release and a `.sys`-scope release are the SAME
// strength (Release) yet wildly different visibility + cost; a `.cta` GPU
// fence and a `DMB ISH` ARM fence are mutually un-substitutable.  The
// two-trunk partial order — NOT a chain, NOT the strength lattice — is the
// only structure expressing both intra-trunk subsumption AND cross-trunk
// incompatibility.
//
// THE LOAD-BEARING PROPERTY this TU defends: cross-trunk pairs are MUTUALLY
// INCOMPARABLE.  The V-267 ScopedFence safety guarantee ("a Cta-scope fence
// is admitted where a Warp requirement is needed but NEVER where a Gpu
// requirement is needed and NEVER for any ARM-domain requirement") reduces
// directly to leq() over this lattice.  If the two trunks were ever
// collapsed into one chain, that guarantee silently evaporates — hence the
// exhaustive cross-trunk negative assertions AND the non-distributivity
// witness below (a single chain WOULD be distributive; this lattice MUST
// NOT be).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace ms  = ::crucible::algebra::lattices::memory_scope;

namespace {

using cal::MemoryScope;
using L = cal::MemoryScopeLattice;

// ── Concept satisfaction — bounded lattice, not a semiring ──────────
static_assert(crucible::algebra::Lattice<L>,
    "FIXY-V-265: MemoryScopeLattice must satisfy the Lattice concept "
    "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>,
    "FIXY-V-265: MemoryScopeLattice has both bottom() (Thread) and top() "
    "(System) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>,
    "FIXY-V-265: MemoryScopeLattice is NOT a semiring — it carries no "
    "equality+add+mul algebra, only the order-theoretic operations.");

// ── Cardinality — 8 scopes (Thread + 4 accel + 2 ARM + System) ──────
static_assert(cal::memory_scope_count == 8,
    "FIXY-V-265: MemoryScope must have exactly 8 enumerators. Adding a "
    "scope requires (a) placing it inside the correct trunk numeric range "
    "so mem_scope_is_accel / mem_scope_is_arm classify it, (b) extending "
    "both name switches, (c) bumping the V-267 ScopedFence satisfies<> + "
    "V-268 collision rules, AND (d) updating kAll[] in the header verifier.");

static_assert(std::is_same_v<std::underlying_type_t<MemoryScope>, std::uint8_t>,
    "FIXY-V-265: MemoryScope must use uint8_t underlying type — the trunk "
    "is packed into the high nibble (accel=0x1_, ARM=0x2_) so within-trunk "
    "integer order equals visibility-width rank.");

// ── Trunk encoding — high nibble carries the trunk ──────────────────
static_assert(std::to_underlying(MemoryScope::Thread) == 0x00, "Thread = bottom sentinel");
static_assert(std::to_underlying(MemoryScope::Warp)   == 0x10, "accel trunk base");
static_assert(std::to_underlying(MemoryScope::Gpu)    == 0x13, "accel trunk top");
static_assert(std::to_underlying(MemoryScope::Inner)  == 0x20, "ARM trunk base");
static_assert(std::to_underlying(MemoryScope::Outer)  == 0x21, "ARM trunk top");
static_assert(std::to_underlying(MemoryScope::System) == 0xFF, "System = top sentinel");

// ── Trunk classification helpers ────────────────────────────────────
static_assert(cal::mem_scope_is_accel(MemoryScope::Warp));
static_assert(cal::mem_scope_is_accel(MemoryScope::Cta));
static_assert(cal::mem_scope_is_accel(MemoryScope::Gpu));
static_assert(!cal::mem_scope_is_accel(MemoryScope::Inner));
static_assert(!cal::mem_scope_is_accel(MemoryScope::Thread),
    "FIXY-V-265: Thread belongs to NEITHER trunk — it is the shared bottom; "
    "same_trunk(Thread, _) must be false so leq special-cases it.");
static_assert(!cal::mem_scope_is_accel(MemoryScope::System));
static_assert(cal::mem_scope_is_arm(MemoryScope::Inner));
static_assert(cal::mem_scope_is_arm(MemoryScope::Outer));
static_assert(!cal::mem_scope_is_arm(MemoryScope::Cta));
static_assert(!cal::mem_scope_is_arm(MemoryScope::System));
static_assert(cal::mem_scope_same_trunk(MemoryScope::Warp, MemoryScope::Gpu));
static_assert(cal::mem_scope_same_trunk(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!cal::mem_scope_same_trunk(MemoryScope::Cta, MemoryScope::Inner),
    "FIXY-V-265: accel × ARM is the cross-trunk case — the load-bearing "
    "non-distributivity / incomparability source.");

// ── Bounds ──────────────────────────────────────────────────────────
static_assert(L::bottom() == MemoryScope::Thread);
static_assert(L::top()    == MemoryScope::System);

// ── Accel trunk subsumption chain (intra-trunk total order) ─────────
// Warp ⊑ Cta ⊑ Cluster ⊑ Gpu
static_assert(L::leq(MemoryScope::Warp,    MemoryScope::Cta));
static_assert(L::leq(MemoryScope::Cta,     MemoryScope::Cluster));
static_assert(L::leq(MemoryScope::Cluster, MemoryScope::Gpu));
static_assert(L::leq(MemoryScope::Warp,    MemoryScope::Gpu), "transitive endpoints");
// The docblock use-case witness: a device fence covers a block requirement;
// a warp fence does not.
static_assert(L::leq(MemoryScope::Cta,  MemoryScope::Gpu),
    "FIXY-V-265: Cta ⊑ Gpu — a Cta-scope requirement IS satisfied by a "
    "device-wide fence.");
static_assert(!L::leq(MemoryScope::Gpu, MemoryScope::Cta),
    "FIXY-V-265: Gpu ⋢ Cta — a device-wide requirement is NOT satisfied by "
    "a block-scope fence; the peer would read stale.");

// ── ARM trunk subsumption chain (intra-trunk total order) ───────────
// Inner (ISH) ⊑ Outer (OSH)
static_assert(L::leq(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer, MemoryScope::Inner), "descending is false");

// ── Thread ⊑ everything ⊑ System ────────────────────────────────────
static_assert(L::leq(MemoryScope::Thread, MemoryScope::Cta));
static_assert(L::leq(MemoryScope::Thread, MemoryScope::Outer));
static_assert(L::leq(MemoryScope::Thread, MemoryScope::System));
static_assert(L::leq(MemoryScope::Gpu,    MemoryScope::System));
static_assert(L::leq(MemoryScope::Outer,  MemoryScope::System));

// ── THE LOAD-BEARING NEGATIVE — cross-trunk incomparability ─────────
//
// Every accel × ARM pair MUST be mutually incomparable in BOTH directions.
// This is the structural fact the ScopedFence safety guarantee rests on: a
// GPU-scope fence is never admitted for an ARM-domain requirement and vice
// versa.
static_assert(!L::leq(MemoryScope::Cta,     MemoryScope::Inner));
static_assert(!L::leq(MemoryScope::Inner,   MemoryScope::Cta));
static_assert(!L::leq(MemoryScope::Gpu,     MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer,   MemoryScope::Gpu));
static_assert(!L::leq(MemoryScope::Warp,    MemoryScope::Inner));
static_assert(!L::leq(MemoryScope::Inner,   MemoryScope::Warp));
static_assert(!L::leq(MemoryScope::Cluster, MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer,   MemoryScope::Cluster));

// ── Reverse sentinels — System ⋢ X, X ⋢ Thread ──────────────────────
static_assert(!L::leq(MemoryScope::System, MemoryScope::Cta));
static_assert(!L::leq(MemoryScope::System, MemoryScope::Thread));
static_assert(!L::leq(MemoryScope::Cta,    MemoryScope::Thread));
static_assert(!L::leq(MemoryScope::Outer,  MemoryScope::Thread));

// ── Join — intra-trunk = wider; cross-trunk = System ────────────────
static_assert(L::join(MemoryScope::Warp, MemoryScope::Gpu)   == MemoryScope::Gpu);
static_assert(L::join(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Outer);
static_assert(L::join(MemoryScope::Cta, MemoryScope::Inner)  == MemoryScope::System,
    "FIXY-V-265: cross-trunk join = System — the only common upper bound of "
    "a GPU scope and an ARM domain is full-system visibility.");
static_assert(L::join(MemoryScope::Gpu, MemoryScope::Outer)  == MemoryScope::System);
static_assert(L::join(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Cta,
    "Thread is the join identity");
static_assert(L::join(MemoryScope::System, MemoryScope::Outer) == MemoryScope::System,
    "System absorbs in join");

// ── Meet — intra-trunk = narrower; cross-trunk = Thread ─────────────
static_assert(L::meet(MemoryScope::Warp, MemoryScope::Gpu)   == MemoryScope::Warp);
static_assert(L::meet(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Inner);
static_assert(L::meet(MemoryScope::Cta, MemoryScope::Inner)  == MemoryScope::Thread,
    "FIXY-V-265: cross-trunk meet = Thread — the only common lower bound of "
    "a GPU scope and an ARM domain is thread-local visibility.");
static_assert(L::meet(MemoryScope::Gpu, MemoryScope::Outer)  == MemoryScope::Thread);
static_assert(L::meet(MemoryScope::System, MemoryScope::Outer) == MemoryScope::Outer,
    "System is the meet identity");
static_assert(L::meet(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Thread,
    "Thread absorbs in meet");

// ── NON-DISTRIBUTIVITY (witness re-stated at TU level) ──────────────
//
// (Gpu ∨ Inner) ∧ Outer = System ∧ Outer = Outer
// (Gpu ∧ Outer) ∨ (Inner ∧ Outer) = Thread ∨ Inner = Inner   (Inner ⊑ Outer, both ARM)
// Outer ≠ Inner  ⟹  non-distributive.
//
// A single-chain lattice WOULD be distributive — this assertion is what
// forbids a future "simplification" that collapses the two trunks.
static_assert(
    L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer) == MemoryScope::Outer,
    "FIXY-V-265: LHS of the distributivity test must be Outer.");
static_assert(
    L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer),
            L::meet(MemoryScope::Inner, MemoryScope::Outer)) == MemoryScope::Inner,
    "FIXY-V-265: RHS of the distributivity test must be Inner (Inner ⊑ Outer "
    "because both are ARM-trunk, so Inner ∧ Outer = Inner — NOT Thread).");
static_assert(
    L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer) !=
    L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer),
            L::meet(MemoryScope::Inner, MemoryScope::Outer)),
    "FIXY-V-265: MemoryScopeLattice MUST be non-distributive. If this fires, "
    "the accel + ARM trunks were collapsed into a single chain — DEFEATING "
    "the cross-trunk incomparability the V-267 ScopedFence safety guarantee "
    "depends on.");

// ── At<Scope> singleton sub-lattice — empty element_type, EBO collapse
static_assert(crucible::algebra::Lattice<ms::ThreadScope>);
static_assert(crucible::algebra::Lattice<ms::CtaScope>);
static_assert(crucible::algebra::Lattice<ms::OuterScope>);
static_assert(crucible::algebra::BoundedLattice<ms::SystemScope>);
static_assert(std::is_empty_v<ms::ThreadScope::element_type>,
    "FIXY-V-265: At<Thread>::element_type must be empty so "
    "Graded<Absolute, At<Thread>, P> EBO-collapses to sizeof(P) — a "
    "zero-byte scope annotation at every binding site.");
static_assert(std::is_empty_v<ms::CtaScope::element_type>);
static_assert(std::is_empty_v<ms::OuterScope::element_type>);
static_assert(std::is_empty_v<ms::SystemScope::element_type>);
static_assert(ms::CtaScope::scope    == MemoryScope::Cta,
    "FIXY-V-265: At<S>::scope must equal S at the type level so the V-267 "
    "wrapper reads the pinned scope with no runtime data.");
static_assert(ms::OuterScope::scope  == MemoryScope::Outer);
static_assert(ms::SystemScope::scope == MemoryScope::System);

// ── EBO collapse witness — Graded<Absolute, At<Scope>, P> == sizeof(P)
struct EightByteValue { unsigned long long v{0}; };
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     ms::CtaScope, EightByteValue>)
    == sizeof(EightByteValue),
    "FIXY-V-265: regime-1 EBO collapse — pinning a Cta scope grade adds "
    "zero bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                     ms::SystemScope, int>)
    == sizeof(int));

// ── Name surface ────────────────────────────────────────────────────
static_assert(L::name() == std::string_view{"MemoryScopeLattice"});
static_assert(ms::CtaScope::name()    == std::string_view{"MemoryScopeLattice::At<Cta>"});
static_assert(ms::InnerScope::name()  == std::string_view{"MemoryScopeLattice::At<Inner>"});
static_assert(ms::ThreadScope::name() == std::string_view{"MemoryScopeLattice::At<Thread>"});
static_assert(cal::memory_scope_name(MemoryScope::Gpu) == std::string_view{"Gpu"});

}  // namespace

int main() {
    cal::detail::memory_scope_lattice_self_test::runtime_smoke_test();
    return 0;
}
