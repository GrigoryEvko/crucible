// example_forge_phase: a Forge compiler phase bound as a pure function.
//
// The twelve phases of Forge (FORGE.md section 5) lower the IR001 tensor
// DAG to the IR002 portable kernel DAG.  Each phase takes a snapshot of
// the IR and an arena, builds a new snapshot in the arena, and returns
// it.
//
// A phase reads its input and does not change it.  It builds its output
// in the arena.  This discipline makes the wall-clock budget of Forge
// enforceable: each phase has a hard time limit, and with no in-place
// mutation a retry or a rollback costs nothing.
//
// The binding records that the phase runs on the background thread, that
// it allocates IR nodes in the arena, that it does no in-place mutation,
// and that it is not reentrant, because the phases share one arena.
//
// The contrast with example_custom_kernel.cpp: the kernel is a user
// callable, from the user and tested.  The phase is internal, from
// Crucible and verified.  In the design, the cross-vendor numerics CI of
// MIMIC.md section 41 checks each Forge phase.  A consumer that asks for
// trust_verified then accepts a Forge phase and refuses a user kernel.
//
// Two axes need no atom.  The strict pole of Mutation refuses in-place
// mutation, and the strict pole of Reentrancy refuses a self-call.  So a
// pure phase is the default, and the pack says nothing about either.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
using ::fixy::Axis;
using ::fixy::axis_traits;
using Effect = ::foundation::effects::Effect;

// Stand-ins for the IR.  In production an IR node carries shapes, dtypes
// and a recipe pin.  The grades describe the contract of the phase, not
// the content of the IR, so a placeholder is sufficient.
struct KernelGraph {
    int num_nodes = 0;  // the count of IR002 nodes
    int num_edges = 0;  // the count of producer-consumer edges
    int generation = 0;  // increases by one with each pass, for diagnostics
};

struct Arena {
    // A stand-in for the bump allocator.  The real Arena.h holds a memory
    // block, a generation counter and the bookkeeping of each block.
    std::size_t bump = 0;
};

// The IR at the bit-exact tier.  The Precision axis of fixy::fn names an
// element type or a Higham bound, and no atom on it states a bit-exact
// result.  A bit-exact claim is a claim about the bytes of a value, so it
// rides on the value through the NumericalTier band of fixy/Bands.h.  The
// phase takes and returns the IR at that tier.  That states that the
// phase keeps the numerical recipe that its input pins.
using BitExactGraph = ::fixy::numerical_tier::Bitexact<KernelGraph>;

// Phase D, FUSE.  It merges each chain of ops with one producer and one
// consumer into one IR node.  It reads its input, builds its output in
// the arena and returns it.  The body is a stand-in: the example is about
// the binding and not about the fusion algorithm.
using ForgePhasePtr = BitExactGraph (*)(const BitExactGraph& input, Arena& arena) noexcept;

BitExactGraph fuse_phase_ref(const BitExactGraph& input, Arena& arena) noexcept {
    // A real pass walks the producer-consumer graph.  The stand-in removes
    // about 10% of the nodes and the edges, and increments the generation.
    const KernelGraph& graph = input.peek();
    arena.bump += sizeof(KernelGraph);
    return ::fixy::mint_band<BitExactGraph>(KernelGraph{.num_nodes = graph.num_nodes - graph.num_nodes / 10,
                                                        .num_edges = graph.num_edges - graph.num_edges / 10,
                                                        .generation = graph.generation + 1});
}

// The pack of an internal Forge phase, named one time.
// fixy::mint_fn_for<ForgePhaseBinding> is its door.
//
// The pack names no Space atom.  The phase allocates in the arena that
// its caller passes, and the arena states the bound.  The argument of
// cost_linear is the bound on N, and zero states no bound.
template <class Phase>
using ForgePhaseBinding = ::fixy::fn<Phase,
                                     at::copy,  // a function pointer is free to copy
                                     at::with<Effect::Bg, Effect::Alloc>,  // the background thread, and the arena
                                     at::as_public,  // the pointer holds no secret
                                     at::from_source<source::FromInternal>,  // Crucible wrote the phase
                                     at::trust_verified,  // the cross-vendor numerics CI is its check
                                     at::cost_linear<0>,  // O(N) in the node count of the IR
                                     at::version<2>>;  // the phase lowers to IR002

using BoundForgePhase = ForgePhaseBinding<ForgePhasePtr>;

static_assert(sizeof(BoundForgePhase) == sizeof(ForgePhasePtr), "a binding must be byte-equivalent to its payload");
static_assert(sizeof(BitExactGraph) == sizeof(KernelGraph), "a band must be byte-equivalent to its value");

// The axes that tell a Forge phase from a user callable.  A consumer that
// asks for these grades accepts only an internal phase with no in-place
// mutation, and the compiler checks that at the call site.
static_assert(std::is_same_v<BoundForgePhase::grade_on<Axis::Mutation>, axis_traits<Axis::Mutation>::strict>,
              "a Forge phase does no in-place mutation on its input");
static_assert(std::is_same_v<BoundForgePhase::grade_on<Axis::Reentrancy>, axis_traits<Axis::Reentrancy>::strict>,
              "the phases share one arena, so two calls at the same time race on the bump cursor");
static_assert(std::is_same_v<BoundForgePhase::grade_on<Axis::Trust>, at::trust_verified>,
              "in the design, the cross-vendor numerics CI checks each Forge phase");
static_assert(std::is_same_v<BoundForgePhase::grade_on<Axis::Provenance>, at::from_source<source::FromInternal>>,
              "Crucible wrote the phase, and the user did not supply it");
static_assert(::fixy::band_tier_v<BitExactGraph> == ::fixy::Tolerance::BITEXACT,
              "the phase keeps the bit-exact tier of the IR");

// The version is part of the type.  A consumer that asks for version 1
// refuses this binding, so a newer phase needs a deliberate change at the
// consumer.
static_assert(std::is_same_v<BoundForgePhase::grade_on<Axis::Version>, at::version<2>>);
static_assert(!std::is_same_v<BoundForgePhase::grade_on<Axis::Version>, at::version<1>>);

// A caller needs a context whose row holds Bg and Alloc.
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, BoundForgePhase>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, BoundForgePhase>);

}  // namespace

int main() {
    const BoundForgePhase bound = ::fixy::mint_fn_for<ForgePhaseBinding>(&fuse_phase_ref);

    // One pass of fusion over an IR of 100 nodes and 200 edges.
    const BitExactGraph input =
        ::fixy::mint_band<BitExactGraph>(KernelGraph{.num_nodes = 100, .num_edges = 200, .generation = 0});
    Arena arena{};

    const BitExactGraph fused = bound.value()(input, arena);

    const KernelGraph& before = input.peek();
    const KernelGraph& after = fused.peek();
    std::printf("forge_phase: input %d nodes / %d edges (gen %d) -> fused %d nodes / %d edges (gen %d), "
                "arena bumped %zu bytes\n",
                before.num_nodes, before.num_edges, before.generation, after.num_nodes, after.num_edges,
                after.generation, arena.bump);
    if (after.num_nodes != 90 || after.num_edges != 180 || after.generation != 1) return 1;
    if (arena.bump != sizeof(KernelGraph)) return 2;

    std::printf("BoundForgePhase sizeof = %zu (== sizeof(ForgePhasePtr) %zu)\n", sizeof(BoundForgePhase),
                sizeof(ForgePhasePtr));
    return 0;
}
