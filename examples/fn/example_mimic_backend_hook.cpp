// ════════════════════════════════════════════════════════════════════
// example_mimic_backend_hook — Phase 0 P0-5 / 5 (#1098)
//
// THE PATTERN: A VENDOR-SPECIFIC MIMIC BACKEND EMITTER WRAPPED IN Fn<>
//
// Mimic's per-vendor backends (mimic/nv/, mimic/am/, mimic/tpu/,
// mimic/trn/, mimic/cpu/ — see MIMIC.md) lower IR002 portable
// kernel DAGs to native ISA.  Each backend ships an `emit_kernel`
// function that takes: (a) an IR002 KernelNode, (b) the target
// device's TargetCaps (occupancy / register budgets / smem size /
// MMA shape constraints), and (c) an arena to allocate the
// compiled bytes into.
//
// The substrate must capture: bg-thread execution, ALL THREE
// effect atoms (Bg + Alloc + IO — IO because emit may probe the
// kernel driver via ioctls per HS9), bit-exact precision pinning
// (the per-recipe ReductionDeterminism tier propagates from IR002
// into emission), AND vendor identity at the type level.
//
// THE KEY INSIGHT: vendor identity flows through the Source axis
// (nominally source::FromInternal, but the substrate's planned
// subdivision lets us narrow to source::FromMimicNvidia /
// FromMimicAmd / etc. once those tags ship).  For Phase 0 we use
// source::FromInternal as the catch-all and demonstrate the
// `EffectRow<Bg, Alloc, IO>` triple — the largest effect row in
// the example set.
//
// CONTRAST: this binding has the BIGGEST EffectRow (3 atoms) and
// the strongest set of "vendor-private" provenance.  It also pins
// `Mutation = Mutable` because the emitter writes compiled bytes
// into the output buffer, and `Reentrancy = Reentrant` because
// per-kernel compilation can run in parallel across the kernel
// compile pool.
// ════════════════════════════════════════════════════════════════════

#include <crucible/safety/Fn.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in IR002 kernel node + target caps ───────────────────────
//
// In production these are the real types from
// crucible/forge/KernelNode.h and crucible/mimic/TargetCaps.h.

struct KernelNode {
    int    kernel_kind;     // GEMM / CONV / SDPA / etc.
    int    tile_m, tile_n, tile_k;
    int    recipe_id;       // index into the recipe registry
};

struct TargetCaps {
    int      sm_count;
    int      regs_per_thread_max;
    int      smem_per_block_kb;
    std::uint32_t arch_id;  // sm_90 / gfx1100 / etc.
};

struct Arena {
    std::size_t bump = 0;
};

struct CompiledBytes {
    void*       ptr;
    std::size_t size;
};

// ── Mimic emit_kernel signature ────────────────────────────────────

using EmitKernelPtr = CompiledBytes(*)(const KernelNode& kernel,
                                       const TargetCaps& caps,
                                       Arena&            arena);

CompiledBytes emit_nv_gemm_ref(const KernelNode& kernel,
                               const TargetCaps& caps,
                               Arena&            arena) noexcept {
    // A real NV emitter would generate SASS via the Mimic NV
    // backend's instruction selector, register allocator, and
    // peephole optimizer.  Stand-in: report a plausible byte
    // count and bump the arena to mimic the allocation.
    const std::size_t n_bytes = 4096;  // ~1 page of SASS for a small GEMM
    arena.bump += n_bytes;
    (void)kernel;  // would drive instruction selection in production
    (void)caps;    // would drive register/smem budgeting
    return CompiledBytes{
        .ptr  = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000)),
        .size = n_bytes
    };
}

// ── The Fn<...> binding ────────────────────────────────────────────
//
// Per-axis grade choices for "Mimic NVIDIA SASS emitter for GEMM
// kernels":
//
//   Type        : EmitKernelPtr                      — function pointer
//   Refinement  : pred::True
//   Usage       : Copy                               — fn ptr is freely copyable
//   EffectRow   : Row<Bg, Alloc, IO>                 — bg + alloc + ioctl probe
//   Security    : SecLevel::Internal                 — sees user IR + caps
//   Protocol    : proto::None
//   Lifetime    : lifetime::Static                   — Mimic-internal free function
//   Source      : source::FromInternal               — Crucible-authored backend
//                                                       (a planned `source::FromMimicNV`
//                                                       sub-tag would narrow this once
//                                                       per-vendor source tags ship)
//   Trust       : trust::Verified                    — cross-vendor CI validates
//                                                       output against the CPU
//                                                       reference oracle (MIMIC.md §41)
//   Repr        : ReprKind::Opaque
//   Cost        : cost::Linear<0>                    — O(N) in IR node count
//   Precision   : precision::Exact                   — emission preserves the IR's
//                                                       recipe pin (BITEXACT_TC /
//                                                       BITEXACT_STRICT) bit-exactly
//   Space       : space::Bounded<0>                  — bounded by arena capacity;
//                                                       compiled-byte cap declared
//                                                       at arena construction
//   Overflow    : OverflowMode::Trap
//   Mutation    : MutationMode::Mutable              — writes compiled bytes into
//                                                       arena-allocated buffer
//   Reentrancy  : ReentrancyMode::Reentrant          — multiple kernels compiled
//                                                       in parallel by the compile
//                                                       pool (one Arena per worker)
//   Size        : size_pol::Unstated
//   Version     : 3                                  — IR003-NV per-vendor IR
//                                                       generation matches Mimic's
//                                                       NV backend version
//   Staleness   : stale::Fresh
//
// THE LOAD-BEARING DELTA from custom_kernel/optimizer/forge_phase:
//   - EffectRow:   Row<Bg, Alloc, IO> — adds IO for driver ioctls.  This
//                  is the LARGEST effect row in the example set.  A
//                  caller that wants to invoke this emitter needs a
//                  context that admits all three effects; pure-bg
//                  contexts (Forge phases) cannot reach it.
//   - Reentrancy:  Reentrant (vs Forge phase NonReentrant) — backend
//                  emission is per-Arena, parallelizable across
//                  multiple kernels in the compile pool.
//   - Version:     3 (vs Forge's 2) — Mimic's per-vendor IR
//                  generation moves at the vendor backend's pace,
//                  decoupled from Forge's IR002 version.

using BoundMimicNvEmit = fn::Fn<
    EmitKernelPtr,                                          // 1 Type
    fn::pred::True,                                         // 2 Refinement
    fn::UsageMode::Copy,                                    // 3 Usage
    fx::Row<fx::Effect::Bg,                                 // 4 EffectRow
            fx::Effect::Alloc,
            fx::Effect::IO>,
    fn::SecLevel::Internal,                                 // 5 Security
    fn::proto::None,                                        // 6 Protocol
    fn::lifetime::Static,                                   // 7 Lifetime
    fn::source::FromInternal,                               // 8 Source
    fn::trust::Verified,                                    // 9 Trust
    fn::ReprKind::Opaque,                                   // 10 Repr
    fn::cost::Linear<0>,                                    // 11 Cost
    fn::precision::Exact,                                   // 12 Precision
    fn::space::Bounded<0>,                                  // 13 Space
    fn::OverflowMode::Trap,                                 // 14 Overflow
    fn::MutationMode::Mutable,                              // 15 Mutation
    fn::ReentrancyMode::Reentrant,                          // 16 Reentrancy
    fn::size_pol::Unstated,                                 // 17 Size
    /*Version=*/3,                                          // 18 Version
    fn::stale::Fresh                                        // 19 Staleness
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundMimicNvEmit) == sizeof(EmitKernelPtr),
    "EBO collapse failed for Mimic NV emit binding.");

// The 3-atom effect row — the largest in the example set.
static_assert(std::is_same_v<BoundMimicNvEmit::effect_row_t,
                             fx::Row<fx::Effect::Bg,
                                     fx::Effect::Alloc,
                                     fx::Effect::IO>>,
    "Mimic emit must declare {Bg, Alloc, IO} — IO is required for "
    "driver ioctl probing per HS9 (no vendor libraries; kernel-driver "
    "ioctls only).");

// Reentrancy distinguishes Mimic emission (parallel-friendly) from
// Forge phases (single-threaded pipeline).
static_assert(BoundMimicNvEmit::reentrancy_v == fn::ReentrancyMode::Reentrant,
    "Mimic emission is parallelizable across the compile pool; "
    "Forge phases share the pipeline arena and are NonReentrant.");

// Trust: Verified — same as Forge phases.  Both subsystems are
// CI-validated against the cross-vendor numerics matrix.
static_assert(std::is_same_v<BoundMimicNvEmit::trust_t, fn::trust::Verified>);

// Version 3 — Mimic's per-vendor IR generation moves on its own
// schedule, independent of Forge's IR002 version.
static_assert(BoundMimicNvEmit::version_v == 3);

}  // namespace

int main() {
    BoundMimicNvEmit bound{emit_nv_gemm_ref};

    // Simulate compiling one GEMM kernel for sm_90.
    const KernelNode kernel{
        .kernel_kind = 1,        // GEMM
        .tile_m      = 128,
        .tile_n      = 128,
        .tile_k      = 32,
        .recipe_id   = 7         // BITEXACT_TC for sm_90 wmma-fp16
    };
    const TargetCaps caps{
        .sm_count             = 132,
        .regs_per_thread_max  = 255,
        .smem_per_block_kb    = 228,
        .arch_id              = 900       // sm_90
    };
    Arena arena{};

    CompiledBytes out = bound.value()(kernel, caps, arena);

    std::printf("mimic_nv_emit: kernel kind=%d tile=%dx%dx%d recipe=%d "
                "→ %zu bytes (arena bumped %zu)\n",
                kernel.kernel_kind, kernel.tile_m, kernel.tile_n,
                kernel.tile_k, kernel.recipe_id, out.size, arena.bump);

    std::printf("BoundMimicNvEmit sizeof = %zu (== sizeof(EmitKernelPtr) %zu)\n",
                sizeof(BoundMimicNvEmit), sizeof(EmitKernelPtr));
    return 0;
}
