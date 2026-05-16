// ════════════════════════════════════════════════════════════════════
// example_fixy_forge_phase — FIXY-E / Phase E worked example 3/4
//
// THE PATTERN: A FORGE COMPILER PHASE, via fixy::fn
//
// Reject-by-default analogue of
//   examples/fn/example_forge_phase.cpp
// (the substrate-direct version using `safety::fn::Fn<...>`).
//
// Forge's 12-phase pipeline (FORGE.md §5) lowers IR001 to IR002.
// Each phase is PURE-FUNCTIONAL on input (Immutable strict default
// holds) and CI-validated (trust::Verified strict default holds).
//
// THE LOAD-BEARING CONTRAST from example_fixy_custom_kernel and
// example_fixy_custom_optimizer:
//   - Mutation:   STRICT (Immutable).  Phases never mutate input IR.
//                 The strict-acknowledgement carries the intent.
//   - Trust:      STRICT (Verified).  Forge phases live in the
//                 crucible/forge/ tree under CI gates (FORGE.md §41
//                 cross-vendor numerics matrix).  No relaxation.
//   - Source:     STRICT (FromInternal).  Forge is Crucible-authored.
//   - Precision:  STRICT (Exact).  IR transformations preserve the
//                 recipe pin bit-exactly.
//   - Version:    RELAXED to 2 (IR002 generation).  Downstream
//                 consumers that pin Version=1 reject this binding.
//
// FIVE RELAXATIONS, FIFTEEN STRICT-ACKS — the smallest relaxation
// surface of the four fixy examples.  Forge phases are the
// "discipline-maximal" shape in the production codebase.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cstddef>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in IR types ──────────────────────────────────────────────

struct KernelGraph {
    int num_nodes = 0;
    int num_edges = 0;
    int generation = 0;
};

struct Arena {
    std::size_t bump = 0;
};

// ── Phase D FUSE signature ─────────────────────────────────────────

using ForgePhasePtr = KernelGraph(*)(const KernelGraph& input, Arena& arena);

KernelGraph fuse_phase_ref(const KernelGraph& input, Arena& arena) noexcept {
    arena.bump += sizeof(KernelGraph);
    return KernelGraph{
        .num_nodes  = input.num_nodes - input.num_nodes / 10,
        .num_edges  = input.num_edges - input.num_edges / 10,
        .generation = input.generation + 1
    };
}

// ── fixy::fn binding — per-dim engagement choices ──────────────────
//
// 5 relaxations + 15 strict-acks.  The strict-acks are LOAD-BEARING
// here — they communicate "Forge phases are NOT user-supplied,
// NOT mutating, NOT lossy-precision".  A reviewer reads the
// strict-acks as authoritatively as the relaxations.

using BoundForgePhase = cf::fn<ForgePhasePtr,
    // 1. Type — substrate carries ForgePhasePtr.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True default.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable.
    cg::copy,

    // 4. Effect = Row<Bg, Alloc> — bg thread + arena allocation.
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,

    // 5. Security — phase sees user IR (model graph); Classified
    //    strict default is correct.
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime — free function, valid forever.
    cf::accept_default_strict_for<cd::Lifetime>,

    // 8. Provenance — STRICT (FromInternal).  Forge is Crucible-
    //    authored, not user-supplied.  Strict-ack here is LOAD-
    //    BEARING; a relaxation to FromUser would lie about the
    //    binding's audit trail.
    cf::accept_default_strict_for<cd::Provenance>,

    // 9. Trust — STRICT (Verified).  Forge phases are CI-validated
    //    against the cross-vendor numerics matrix (FORGE.md §41).
    //    Strict-ack here is the WHOLE POINT of using a Forge phase
    //    binding instead of a user-supplied callable.
    cf::accept_default_strict_for<cd::Trust>,

    // 10. Representation — Opaque strict default.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Linear<0> — O(N) in IR node count.
    cg::complexity_linear<0>,

    // 13. Precision — STRICT (Exact).  IR transformations preserve
    //     the recipe pin bit-exactly; relaxing to F32 would forfeit
    //     BITEXACT_STRICT compatibility.
    cf::accept_default_strict_for<cd::Precision>,

    // 14. Space = Bounded<0> — bounded by arena capacity (the
    //     bound is declared at the arena's construction site).
    cg::space_bounded<0>,

    // 15. Overflow — Trap strict default.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation — STRICT (Immutable).  Phases never mutate input
    //     IR; output is constructed in the arena.  Strict-ack here
    //     is the LOAD-BEARING discipline that makes Forge's
    //     wall-clock budget retryable (no in-place mutation =
    //     free rollback).
    cf::accept_default_strict_for<cd::Mutation>,

    // 17. Reentrancy — STRICT (NonReentrant).  Phases share the
    //     pipeline arena; concurrent calls would race on the bump
    //     cursor.
    cf::accept_default_strict_for<cd::Reentrancy>,

    // 18. Size — Unstated strict default.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version = 2 — IR002 phase generation.  Downstream
    //     consumers that pin Version=1 reject this binding, forcing
    //     a deliberate version bump rather than silent acceptance.
    cg::version<2>,

    // 20. Staleness — Fresh strict default.
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundForgePhase) == sizeof(ForgePhasePtr),
    "EBO collapse failed for Forge phase binding.");

// The discriminating axes — a downstream consumer that demands
// {Trust=Verified, Mutation=Immutable, Precision=Exact} accepts ONLY
// Forge-internal phases, never user-supplied callables.
static_assert(BoundForgePhase::mutation_v == fn::MutationMode::Immutable,
    "Forge phases must be pure-functional on input.");
static_assert(std::is_same_v<BoundForgePhase::trust_t, fn::trust::Verified>,
    "Forge phases carry CI-verified trust.");
static_assert(std::is_same_v<BoundForgePhase::source_t,
                             ::crucible::safety::source::FromInternal>,
    "Forge phases are Crucible-authored.");
static_assert(std::is_same_v<BoundForgePhase::precision_t,
                             fn::precision::Exact>,
    "Forge phases preserve bit-exact numerics under the IR's recipe pin.");
static_assert(std::is_same_v<BoundForgePhase::cost_t, fn::cost::Linear<0>>);
static_assert(std::is_same_v<BoundForgePhase::space_t, fn::space::Bounded<0>>);
static_assert(std::is_same_v<BoundForgePhase::effect_row_t,
                             fx::Row<fx::Effect::Bg, fx::Effect::Alloc>>);

// Version 2 — the IR002 generation marker.
static_assert(BoundForgePhase::version_v == 2,
    "Phase version drift — downstream consumers must opt in.");

}  // namespace

int main() {
    BoundForgePhase bound{fuse_phase_ref};

    KernelGraph input{
        .num_nodes  = 100,
        .num_edges  = 200,
        .generation = 0
    };
    Arena arena{};

    KernelGraph fused = bound.value()(input, arena);

    std::printf("fixy forge_phase: input %d nodes / %d edges (gen %d) → "
                "fused %d nodes / %d edges (gen %d), arena bumped %zu bytes\n",
                input.num_nodes, input.num_edges, input.generation,
                fused.num_nodes, fused.num_edges, fused.generation,
                arena.bump);

    std::printf("BoundForgePhase sizeof = %zu (== sizeof(ForgePhasePtr) %zu) "
                "[20-dim grade vector, zero runtime cost]\n",
                sizeof(BoundForgePhase), sizeof(ForgePhasePtr));
    return 0;
}
