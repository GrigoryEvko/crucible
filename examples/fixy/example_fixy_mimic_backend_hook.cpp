// ════════════════════════════════════════════════════════════════════
// example_fixy_mimic_backend_hook — FIXY-E / Phase E worked example 4/4
//
// THE PATTERN: A VENDOR-SPECIFIC Mimic BACKEND EMITTER, via fixy::fn
//
// Reject-by-default analogue of
//   examples/fn/example_mimic_backend_hook.cpp
// (the substrate-direct version using `safety::fn::Fn<...>`).
//
// Mimic's per-vendor backends (mimic/nv/, mimic/am/, mimic/tpu/,
// mimic/trn/, mimic/cpu/ — see MIMIC.md) emit native ISA from IR002.
// Each backend ships an `emit_kernel` function bound via this shape.
//
// THE LOAD-BEARING CONTRAST from the other three fixy examples:
//   - EffectRow:   THE LARGEST.  `cg::with<Bg, Alloc, IO>` — three
//                  atoms, because emit may probe the kernel driver
//                  via ioctls per HS9 (no vendor libraries; kernel
//                  driver ioctls only).
//   - Reentrancy:  Reentrant (vs Forge phase NonReentrant).  Backend
//                  emission is per-Arena, parallelizable across the
//                  kernel compile pool.
//   - Mutation:    Mutable.  Emits compiled bytes into the output
//                  buffer.
//   - Version:     3 (vs Forge's 2).  Mimic's per-vendor IR
//                  generation moves on the vendor backend's
//                  schedule, decoupled from Forge's IR002 version.
//
// SEVEN RELAXATIONS — the heaviest of the four examples.  Still:
// `sizeof == sizeof(fnptr)` because every Grant is empty + final +
// EBO-collapsible.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in IR002 kernel node + target caps ──────────────────────

struct KernelNode {
    int    kernel_kind;
    int    tile_m, tile_n, tile_k;
    int    recipe_id;
};

struct TargetCaps {
    int           sm_count;
    int           regs_per_thread_max;
    int           smem_per_block_kb;
    std::uint32_t arch_id;
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
    const std::size_t n_bytes = 4096;  // ~1 page of SASS for a small GEMM
    arena.bump += n_bytes;
    (void)kernel;
    (void)caps;
    return CompiledBytes{
        .ptr  = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000)),
        .size = n_bytes
    };
}

// ── fixy::fn binding — per-dim engagement choices ──────────────────
//
// 7 relaxations + 13 strict-acks.  THE LARGEST EFFECT ROW
// (`Row<Bg, Alloc, IO>`) is the binding's signature feature.

using BoundMimicNvEmit = cf::fn<EmitKernelPtr,
    // 1. Type — substrate carries EmitKernelPtr.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True default.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable.
    cg::copy,

    // 4. Effect = Row<Bg, Alloc, IO> — bg + alloc + ioctl probe.
    //    IO is REQUIRED per HS9 (no vendor libraries; kernel-driver
    //    ioctls only).  THE LARGEST EFFECT ROW IN THE EXAMPLE SET.
    cg::with<fx::Effect::Bg, fx::Effect::Alloc, fx::Effect::IO>,

    // 5. Security — emitter sees user IR + caps; Classified strict
    //    default is correct.
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime — free function, valid forever.
    cf::accept_default_strict_for<cd::Lifetime>,

    // 8. Provenance — STRICT (FromInternal).  Mimic backends are
    //    Crucible-authored.  A planned per-vendor sub-tag
    //    (`source::FromMimicNV`) would narrow this once that tag
    //    family ships.
    cf::accept_default_strict_for<cd::Provenance>,

    // 9. Trust — STRICT (Verified).  Cross-vendor numerics CI
    //    validates output against the CPU reference oracle
    //    (MIMIC.md §41).
    cf::accept_default_strict_for<cd::Trust>,

    // 10. Representation — Opaque strict default.  A future
    //     `cg::vendor<mimic::nv::tag>` would engage Representation
    //     with vendor identity at the type level; Phase B vocab
    //     defers.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Linear<1> — O(1·N) in IR node count.  N is
    //     the per-element multiplier; the actual N is the IR's node
    //     count, declared at the call site.
    cg::complexity_linear<1>,

    // 13. Precision — STRICT (Exact).  Emission preserves the IR's
    //     recipe pin (BITEXACT_TC / BITEXACT_STRICT) bit-exactly.
    cf::accept_default_strict_for<cd::Precision>,

    // 14. Space = Bounded<sizeof(CompiledBytes)> — one compiled-bytes
    //     descriptor per emission.  Declared at the arena's bump site.
    cg::space_bounded<sizeof(CompiledBytes)>,

    // 15. Overflow — Trap strict default.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation = Mutable — writes compiled bytes into the
    //     arena-allocated buffer.
    cg::mutable_in_place,

    // 17. Reentrancy = Reentrant — multiple kernels compiled in
    //     parallel by the compile pool (one Arena per worker).
    //     CONTRAST: Forge phase is NonReentrant (shares the
    //     pipeline arena).
    cg::reentrant,

    // 18. Size — Unstated strict default.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version = 3 — IR003-NV per-vendor IR generation; matches
    //     Mimic's NV backend version.  Decoupled from Forge's
    //     IR002 version (= 2 in example_fixy_forge_phase).
    cg::version<3>,

    // 20. Staleness — Fresh strict default.
    cf::accept_default_strict_for<cd::Staleness>
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
    "driver ioctl probing per HS9 (kernel-driver ioctls only).");

// Reentrancy distinguishes Mimic emission from Forge phases.
static_assert(BoundMimicNvEmit::reentrancy_v == fn::ReentrancyMode::Reentrant,
    "Mimic emission is parallelizable across the compile pool.");

// Mutation distinguishes Mimic emission (writes bytes) from Forge
// phases (pure-functional).
static_assert(BoundMimicNvEmit::mutation_v == fn::MutationMode::Mutable);

// Trust: Verified — same as Forge phases.  Both subsystems are
// CI-validated against the cross-vendor numerics matrix.
static_assert(std::is_same_v<BoundMimicNvEmit::trust_t, fn::trust::Verified>);

// Version 3 — Mimic's per-vendor IR generation moves on its own
// schedule, independent of Forge's IR002 version.
static_assert(BoundMimicNvEmit::version_v == 3);

}  // namespace

int main() {
    BoundMimicNvEmit bound{emit_nv_gemm_ref};

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

    std::printf("fixy mimic_nv_emit: kernel kind=%d tile=%dx%dx%d recipe=%d "
                "→ %zu bytes (arena bumped %zu)\n",
                kernel.kernel_kind, kernel.tile_m, kernel.tile_n,
                kernel.tile_k, kernel.recipe_id, out.size, arena.bump);

    std::printf("BoundMimicNvEmit sizeof = %zu (== sizeof(EmitKernelPtr) %zu) "
                "[20-dim grade vector w/ 3-atom EffectRow, zero runtime cost]\n",
                sizeof(BoundMimicNvEmit), sizeof(EmitKernelPtr));
    return 0;
}
