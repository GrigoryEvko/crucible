// ════════════════════════════════════════════════════════════════════
// example_forge_phase — Phase 0 P0-5 / 5 (#1098)
//
// THE PATTERN: A FORGE COMPILER PHASE WRAPPED IN Fn<>
//
// Forge's 12-phase pipeline (FORGE.md §5) lowers an IR001 tensor
// DAG to an IR002 portable kernel DAG.  Each phase is a function
// that takes a snapshot of the IR + an arena, produces a NEW
// snapshot in the arena, and returns the new IR.
//
// Phases are PURE-FUNCTIONAL: they read the input IR but never
// mutate it; the output IR is constructed in the arena.  This
// discipline is what makes Forge's wall-clock budget enforceable
// (each phase has a hard time limit; no in-place mutation means
// retry / rollback is free).
//
// The substrate must capture: bg-thread execution, allocation row
// (every phase allocates new IR nodes in the arena), pure-functional
// mutation discipline (Immutable on input), bit-exact precision
// (IR transformations preserve numerical semantics), AND the
// non-reentrancy that follows from sharing the arena across the
// phase pipeline.
//
// CONTRAST: example_custom_kernel binds a USER-supplied kernel
// (source::FromUser, trust::Tested).  This binds an INTERNAL Forge
// phase (source::FromInternal, trust::Verified — Forge phases are
// CI-validated against the cross-vendor numerics matrix per
// FORGE.md §41).
//
// THE KEY INSIGHT: same Fn<...> template, same callable shape, but
// the trust axis flips from Tested → Verified because Forge phases
// have a CROSS-VENDOR NUMERICS CI test, not just unit tests.  The
// type system carries the distinction so a downstream consumer that
// demands `Trust = trust::Verified` (e.g., the BITEXACT_STRICT
// recipe path) accepts only Forge-internal callables.
// ════════════════════════════════════════════════════════════════════

#include <crucible/safety/Fn.h>

#include <cstddef>
#include <cstdio>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in IR types ──────────────────────────────────────────────
//
// In production the IR nodes carry shapes, dtypes, recipe pins, etc.
// For the example, a minimal placeholder is enough — the binding's
// per-axis grades describe the FUNCTION's contract, not the IR's
// internal richness.

struct KernelGraph {
    int num_nodes = 0;     // count of IR002 nodes
    int num_edges = 0;     // count of producer-consumer edges
    int generation = 0;    // monotonic across passes (for diagnostics)
};

struct Arena {
    // Bump-pointer allocator placeholder; a real Arena.h carries
    // a memory block, generation counter, and per-block bookkeeping.
    std::size_t bump = 0;
};

// ── Forge phase signature ──────────────────────────────────────────
//
// Phase D FUSE — example phase that fuses adjacent ops with single
// producer-consumer chains.  Reads input IR (immutably), writes
// output IR (newly allocated in the arena), returns the result.
//
// The reference implementation here is intentionally trivial — the
// example is about the BINDING, not the fusion algorithm.

using ForgePhasePtr = KernelGraph(*)(const KernelGraph& input, Arena& arena);

KernelGraph fuse_phase_ref(const KernelGraph& input, Arena& arena) noexcept {
    // A real fuse pass walks the producer-consumer graph and merges
    // single-chain ops into one IR node.  Stand-in: increment
    // generation, decrement node count by ~10% to mimic fusion.
    arena.bump += sizeof(KernelGraph);
    return KernelGraph{
        .num_nodes  = input.num_nodes - input.num_nodes / 10,
        .num_edges  = input.num_edges - input.num_edges / 10,
        .generation = input.generation + 1
    };
}

// ── The Fn<...> binding ────────────────────────────────────────────
//
// Per-axis grade choices for "Forge Phase D FUSE":
//
//   Type        : ForgePhasePtr                      — function pointer
//   Refinement  : pred::True
//   Usage       : Copy                               — fn ptr is freely copyable
//   EffectRow   : Row<Bg, Alloc>                     — bg thread + arena alloc
//   Security    : SecLevel::Internal                 — sees user IR (model graph)
//   Protocol    : proto::None
//   Lifetime    : lifetime::Static                   — Forge-internal free function
//   Source      : source::FromInternal               — Crucible-authored
//   Trust       : trust::Verified                    — CI-validated against
//                                                       cross-vendor numerics matrix
//                                                       (FORGE.md §41)
//   Repr        : ReprKind::Opaque
//   Cost        : cost::Linear<0>                    — O(N) in IR node count
//   Precision   : precision::Exact                   — IR transformations are
//                                                       bit-exact (BITEXACT_STRICT
//                                                       under the recipe registry)
//   Space       : space::Bounded<0>                  — bounded by arena capacity
//                                                       (the actual bound is
//                                                       declared at the arena's
//                                                       construction site)
//   Overflow    : OverflowMode::Trap
//   Mutation    : MutationMode::Immutable            — phase is PURE-FUNCTIONAL
//                                                       on input; output is newly
//                                                       constructed in arena
//   Reentrancy  : ReentrancyMode::NonReentrant       — phases share the pipeline
//                                                       arena; concurrent calls
//                                                       would race on bump cursor
//   Size        : size_pol::Unstated
//   Version     : 2                                  — IR002 phase version
//   Staleness   : stale::Fresh
//
// THE LOAD-BEARING DELTA from custom_kernel/optimizer:
//   - Mutation: Immutable (vs Mutable) — phases are pure functions
//   - Trust:    Verified  (vs Tested)  — CI-validated numerics
//   - Source:   FromInternal (vs FromUser) — Crucible-authored
//   - Precision: Exact     (vs F32)    — IR is dtype-agnostic; the
//                                         phase preserves whatever
//                                         numerical recipe the IR
//                                         already pinned

using BoundForgePhase = fn::Fn<
    ForgePhasePtr,                              // 1 Type
    fn::pred::True,                             // 2 Refinement
    fn::UsageMode::Copy,                        // 3 Usage
    fx::Row<fx::Effect::Bg, fx::Effect::Alloc>, // 4 EffectRow
    fn::SecLevel::Internal,                     // 5 Security
    fn::proto::None,                            // 6 Protocol
    fn::lifetime::Static,                       // 7 Lifetime
    fn::source::FromInternal,                   // 8 Source
    fn::trust::Verified,                        // 9 Trust — CI-VERIFIED
    fn::ReprKind::Opaque,                       // 10 Repr
    fn::cost::Linear<0>,                        // 11 Cost — O(N)
    fn::precision::Exact,                       // 12 Precision
    fn::space::Bounded<0>,                      // 13 Space — bounded by arena
    fn::OverflowMode::Trap,                     // 14 Overflow
    fn::MutationMode::Immutable,                // 15 Mutation — PURE-FUNCTIONAL
    fn::ReentrancyMode::NonReentrant,           // 16 Reentrancy
    fn::size_pol::Unstated,                     // 17 Size
    /*Version=*/2,                              // 18 Version — IR002 generation
    fn::stale::Fresh                            // 19 Staleness
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundForgePhase) == sizeof(ForgePhasePtr),
    "EBO collapse failed for Forge phase binding.");

// The discriminating axes — a downstream consumer that demands
// `Trust = Verified` AND `Mutation = Immutable` accepts ONLY
// Forge-internal pure-functional phases, never user-supplied
// mutating callables.  The compiler enforces this at the call site.
static_assert(BoundForgePhase::mutation_v == fn::MutationMode::Immutable,
    "Forge phases must be pure-functional on input.");
static_assert(std::is_same_v<BoundForgePhase::trust_t, fn::trust::Verified>,
    "Forge phases carry CI-verified trust (cross-vendor numerics matrix).");
static_assert(std::is_same_v<BoundForgePhase::source_t, fn::source::FromInternal>,
    "Forge phases are Crucible-authored, not user-supplied.");
static_assert(std::is_same_v<BoundForgePhase::precision_t, fn::precision::Exact>,
    "Forge phases preserve bit-exact numerics under the IR's recipe pin.");

// Version > 1 — the phase has been revised.  Downstream consumers
// that pin Version = 1 reject this binding, forcing a deliberate
// version bump rather than silent acceptance of newer phase output.
static_assert(BoundForgePhase::version_v == 2,
    "Phase version drift — downstream consumers must opt in.");

}  // namespace

int main() {
    BoundForgePhase bound{fuse_phase_ref};

    // Simulate a Forge pipeline: input IR with 100 nodes / 200 edges
    // through one fusion pass.
    KernelGraph input{
        .num_nodes  = 100,
        .num_edges  = 200,
        .generation = 0
    };
    Arena arena{};

    KernelGraph fused = bound.value()(input, arena);

    std::printf("forge_phase: input %d nodes / %d edges (gen %d) → "
                "fused %d nodes / %d edges (gen %d), arena bumped %zu bytes\n",
                input.num_nodes, input.num_edges, input.generation,
                fused.num_nodes, fused.num_edges, fused.generation,
                arena.bump);

    std::printf("BoundForgePhase sizeof = %zu (== sizeof(ForgePhasePtr) %zu)\n",
                sizeof(BoundForgePhase), sizeof(ForgePhasePtr));
    return 0;
}
