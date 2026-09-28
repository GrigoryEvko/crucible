// example_mimic_backend_hook: the kernel emitter of one vendor backend.
//
// Each backend of Mimic (mimic/nv/, mimic/am/, mimic/tpu/, mimic/trn/,
// mimic/cpu/, refer to MIMIC.md) lowers a portable IR002 kernel DAG to
// native ISA.  Each backend has an emit_kernel function.  It takes an
// IR002 KernelNode, the TargetCaps of the device (occupancy, register
// budget, shared memory, MMA shape constraints) and an arena for the
// compiled bytes.
//
// The binding records that the emitter runs on the background thread, and
// it records three effect atoms: Bg, Alloc and IO.  IO is necessary
// because the emitter can probe the kernel driver through ioctls, and
// HS9 permits no vendor library.  The binding also records the vendor of
// the emitter in its type.
//
// The vendor rides on the payload through the Vendor band of
// fixy/Bands.h.  The binding is over fixy::vendor::Nv<EmitKernelPtr>, so
// the type says that the emitter builds for NVIDIA.  A consumer that asks
// for an AMD emitter refuses it, and an NVIDIA emitter and an AMD emitter
// with the same pack take two federation cache slots.
//
// The contrast with the other examples: this binding has the largest
// effect row, three atoms.  A caller needs a context that admits all
// three, so the background drain context, which admits a Forge phase,
// cannot call the emitter.  The binding is reentrant, because the compile
// pool emits kernels in parallel, with one arena for each worker.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
using ::fixy::Axis;
using Effect = ::foundation::effects::Effect;

// Stand-ins for the IR002 kernel node and the target caps.  In
// production the kernel node of Forge and the capabilities that the
// calibration of Meridian measures for the device take their place.
struct KernelNode {
    int kernel_kind = 0;  // GEMM, CONV, SDPA and the other kinds
    int tile_m = 0, tile_n = 0, tile_k = 0;
    int recipe_id = 0;  // the index into the recipe registry
};

struct TargetCaps {
    int sm_count = 0;
    int regs_per_thread_max = 0;
    int smem_per_block_kb = 0;
    std::uint32_t arch_id = 0;  // sm_90, gfx1100 and the other architectures
};

struct Arena {
    std::size_t bump = 0;
};

// Where the compiled bytes are in the arena.
struct CompiledBytes {
    std::size_t offset = 0;
    std::size_t size = 0;
};

using EmitKernelPtr = CompiledBytes (*)(const KernelNode& kernel, const TargetCaps& caps, Arena& arena) noexcept;

CompiledBytes emit_nv_gemm_ref(const KernelNode& kernel, const TargetCaps& caps, Arena& arena) noexcept {
    // A real NVIDIA emitter makes SASS through the instruction selector,
    // the register allocator and the peephole optimizer of the backend.
    // The stand-in reports a plausible byte count and bumps the arena.
    const std::size_t n_bytes = 4096;  // about one page of SASS for a small GEMM
    const std::size_t offset = arena.bump;
    arena.bump += n_bytes;
    (void)kernel;  // drives the instruction selection in production
    (void)caps;  // drives the register and shared memory budget
    return CompiledBytes{.offset = offset, .size = n_bytes};
}

// The emitter, with the vendor that it builds for in its type.
using NvEmitKernel = ::fixy::vendor::Nv<EmitKernelPtr>;

// The pack of a backend emitter, named one time.
// fixy::mint_fn_for<BackendEmitter> is its door.
//
// The pack names no Space atom and no Precision atom.  The emitter writes
// into the arena that its caller passes, and the arena states the bound.
// The numerical recipe rides on the IR that the emitter reads, as
// example_forge_phase.cpp shows.  The argument of cost_linear is the
// bound on N, and zero states no bound.
template <class Emitter>
using BackendEmitter = ::fixy::fn<Emitter,
                                  at::copy,  // a function pointer is free to copy
                                  at::with<Effect::Bg, Effect::Alloc, Effect::IO>,  // plus IO for the driver ioctls
                                  at::as_public,  // the pointer holds no secret
                                  at::from_source<source::FromInternal>,  // Crucible wrote the backend
                                  at::trust_verified,  // the CI against the CPU oracle is its check
                                  at::cost_linear<0>,  // O(N) in the node count of the IR
                                  at::mut_mutable,  // the emitter writes compiled bytes into the arena
                                  at::reentrant,  // the compile pool emits kernels in parallel
                                  at::version<3>>;  // the generation of the per-vendor IR003 of the backend

using BoundMimicNvEmit = BackendEmitter<NvEmitKernel>;

static_assert(sizeof(BoundMimicNvEmit) == sizeof(EmitKernelPtr),
              "a binding over a band must be byte-equivalent to the function pointer");

// The three-atom row, the largest in the example set.  A row is a set, and
// binding_row_t gives it in the order of the enum.
static_assert(std::is_same_v<::fixy::binding_row_t<BoundMimicNvEmit>,
                             ::foundation::effects::Row<Effect::Alloc, Effect::IO, Effect::Bg>>,
              "the emitter must declare Bg, Alloc and IO.  IO is necessary for the driver ioctls of HS9: no "
              "vendor library, only the ioctls of the kernel driver.");

// The compile context admits the row.  The drain context, which admits a
// Forge phase, lacks IO and refuses the emitter.
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, BoundMimicNvEmit>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, BoundMimicNvEmit>);

// Emission can run in parallel.  Forge phases share one arena and are not
// reentrant.
static_assert(std::is_same_v<BoundMimicNvEmit::grade_on<Axis::Reentrancy>, at::reentrant>);
static_assert(std::is_same_v<BoundMimicNvEmit::grade_on<Axis::Trust>, at::trust_verified>);

// The generation of the IR of the backend moves at its own pace,
// separately from the IR002 version of Forge.
static_assert(std::is_same_v<BoundMimicNvEmit::grade_on<Axis::Version>, at::version<3>>);

// The vendor is in the type.  An NVIDIA emitter serves an NVIDIA consumer
// and refuses an AMD one.  An AMD emitter with the same pack takes a
// different federation cache slot, because the payload of the binding is
// a part of the key.
static_assert(::fixy::satisfies_v<NvEmitKernel, ::fixy::VendorBackend_v::NV>);
static_assert(!::fixy::satisfies_v<NvEmitKernel, ::fixy::VendorBackend_v::AMD>);
static_assert(::foundation::diag::row_hash_contribution_v<BoundMimicNvEmit>
                  != ::foundation::diag::row_hash_contribution_v<BackendEmitter<::fixy::vendor::Amd<EmitKernelPtr>>>,
              "two vendors must take two cache slots, or a kernel for one vendor serves the other");

}  // namespace

int main() {
    const BoundMimicNvEmit bound =
        ::fixy::mint_fn_for<BackendEmitter>(::fixy::mint_band<NvEmitKernel>(&emit_nv_gemm_ref));

    // Compile one GEMM kernel for sm_90.
    const KernelNode kernel{
        .kernel_kind = 1,  // GEMM
        .tile_m = 128,
        .tile_n = 128,
        .tile_k = 32,
        .recipe_id = 7  // the index into the recipe registry
    };
    const TargetCaps caps{
        .sm_count = 132,
        .regs_per_thread_max = 255,
        .smem_per_block_kb = 228,
        .arch_id = 900  // sm_90
    };
    Arena arena{};

    const CompiledBytes out = bound.value().peek()(kernel, caps, arena);

    std::printf("mimic_nv_emit: kernel kind=%d tile=%dx%dx%d recipe=%d -> %zu bytes at offset %zu "
                "(arena bumped %zu)\n",
                kernel.kernel_kind, kernel.tile_m, kernel.tile_n, kernel.tile_k, kernel.recipe_id, out.size, out.offset,
                arena.bump);
    if (out.size != 4096 || out.offset != 0 || arena.bump != 4096) return 1;

    std::printf("BoundMimicNvEmit sizeof = %zu (== sizeof(EmitKernelPtr) %zu)\n", sizeof(BoundMimicNvEmit),
                sizeof(EmitKernelPtr));
    return 0;
}
