// ════════════════════════════════════════════════════════════════════
// example_custom_kernel — Phase 0 P0-5 / 5 (#1098)
//
// THE PATTERN: A USER-SUPPLIED KERNEL FUNCTION
//
// A user wants to bind their own dense GEMM kernel to Crucible's
// dispatch path.  They have a function that computes
//
//     C = A @ B            (M × K) @ (K × N) = (M × N)
//
// for FP32 matrices on the bg thread.  The substrate must capture
// the kernel's effect surface, mutation discipline, reentrancy,
// and provenance — all at the type level, all zero runtime cost.
//
// THIS FILE: shows EVERY non-default per-axis grade choice for the
// custom-kernel pattern, with a one-line rationale per axis.  The
// resulting `Fn<KernelPtr, ...>` is byte-equivalent to a bare
// function pointer (`sizeof == sizeof(void*)`) — the 19 type-level
// axes impose ZERO runtime overhead.
//
// WHAT TO READ NEXT: example_custom_optimizer.cpp adds the
// `Cost::Linear<N>` and `Mutation::Mutable` axes for in-place
// updates.  example_forge_phase.cpp shows the SAME callable-binding
// pattern but for a pure-functional IR transformation.
// ════════════════════════════════════════════════════════════════════

#include <crucible/safety/Fn.h>

#include <cstdio>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in kernel signature ───────────────────────────────────────
//
// In production this would be a real function compiled by the user's
// own GEMM library (cublas-replacement, hand-written intrinsics, etc.).
// For the example, we ship a minimal scalar reference that demonstrates
// the binding shape — the per-axis grades describe the FUNCTION's
// behavior, not the implementation quality.

using GemmFp32Ptr = void(*)(const float* a,    // M × K, row-major
                            const float* b,    // K × N, row-major
                            float*       c,    // M × N, row-major (output)
                            int          m,
                            int          n,
                            int          k);

// Scalar reference — the implementation is irrelevant to the example;
// what matters is the SIGNATURE the kernel binding describes.
void scalar_gemm_ref(const float* a, const float* b, float* c,
                     int m, int n, int k) noexcept {
    for (int row = 0; row < m; ++row) {
        for (int col = 0; col < n; ++col) {
            float acc = 0.f;
            for (int p = 0; p < k; ++p) {
                acc += a[row * k + p] * b[p * n + col];
            }
            c[row * n + col] = acc;
        }
    }
}

// ── The Fn<...> binding ────────────────────────────────────────────
//
// Per-axis grade choices for "user-supplied GEMM kernel running on
// the bg thread".  Each axis is annotated with WHY this particular
// grade is correct for this binding.
//
//   Type        : GemmFp32Ptr                — function pointer
//   Refinement  : pred::True                 — no compile-time gate beyond signature
//   Usage       : Copy                       — function pointer is freely copyable
//   EffectRow   : Row<Effect::Bg>            — kernel runs on bg thread only
//   Security    : SecLevel::Internal         — sees user's model weights
//   Protocol    : proto::None                — no session-typed handshake
//   Lifetime    : lifetime::Static           — free function, valid forever
//   Source      : source::FromUser           — the user supplied this kernel
//   Trust       : trust::Tested              — assumes user has tests; no formal proof
//   Repr        : ReprKind::Opaque           — function pointer is an opaque addr
//   Cost        : cost::Unstated             — GEMM is O(MNK) cubic; vocab has only Linear/Quadratic, declare unstated
//   Precision   : precision::F32             — FP32 accumulation
//   Space       : space::Zero                — in-place output, no aux allocs
//   Overflow    : OverflowMode::Trap         — FP overflow → infinity (IEEE 754)
//   Mutation    : MutationMode::Mutable      — writes the C output buffer
//   Reentrancy  : ReentrancyMode::Reentrant  — multiple kernels can run in parallel
//   Size        : size_pol::Unstated         — observation depth N/A for kernel ptr
//   Version     : 1                          — first revision of this binding
//   Staleness   : stale::Fresh               — no time-based decay
//
// At -O3 every axis above EBO-collapses into the function pointer:
//
//   static_assert(sizeof(BoundGemm) == sizeof(GemmFp32Ptr));

using BoundGemm = fn::Fn<
    GemmFp32Ptr,                                // 1 Type
    fn::pred::True,                             // 2 Refinement
    fn::UsageMode::Copy,                        // 3 Usage
    fx::Row<fx::Effect::Bg>,                    // 4 EffectRow
    fn::SecLevel::Internal,                     // 5 Security
    fn::proto::None,                            // 6 Protocol
    fn::lifetime::Static,                       // 7 Lifetime
    fn::source::FromUser,                       // 8 Source
    fn::trust::Tested,                          // 9 Trust
    fn::ReprKind::Opaque,                       // 10 Repr
    fn::cost::Unstated,                         // 11 Cost
    fn::precision::F32,                         // 12 Precision
    fn::space::Zero,                            // 13 Space
    fn::OverflowMode::Trap,                     // 14 Overflow
    fn::MutationMode::Mutable,                  // 15 Mutation
    fn::ReentrancyMode::Reentrant,              // 16 Reentrancy
    fn::size_pol::Unstated,                     // 17 Size
    /*Version=*/1,                              // 18 Version
    fn::stale::Fresh                            // 19 Staleness
>;

// ── Compile-time invariants ────────────────────────────────────────
//
// The substrate's zero-runtime-cost claim: customizing all 19 axes
// must NOT add a single byte beyond the function pointer itself.

static_assert(sizeof(BoundGemm) == sizeof(GemmFp32Ptr),
    "EBO collapse failed — Fn must remain byte-equivalent to its Type "
    "regardless of axis customization.  If this fires, an axis was "
    "implemented as a runtime member instead of a type-level grade.");

// Per-axis introspection — accessors compile to immediate values.
static_assert(BoundGemm::usage_v        == fn::UsageMode::Copy);
static_assert(BoundGemm::security_v     == fn::SecLevel::Internal);
static_assert(BoundGemm::overflow_v     == fn::OverflowMode::Trap);
static_assert(BoundGemm::mutation_v     == fn::MutationMode::Mutable);
static_assert(BoundGemm::reentrancy_v   == fn::ReentrancyMode::Reentrant);
static_assert(BoundGemm::version_v      == 1);
static_assert(std::is_same_v<BoundGemm::source_t,    fn::source::FromUser>);
static_assert(std::is_same_v<BoundGemm::trust_t,     fn::trust::Tested>);
static_assert(std::is_same_v<BoundGemm::precision_t, fn::precision::F32>);
static_assert(std::is_same_v<BoundGemm::effect_row_t,
                             fx::Row<fx::Effect::Bg>>);

}  // namespace

int main() {
    // Materialize the binding from the function pointer.  The mint_fn
    // factory takes the default-grade path, so for full per-axis
    // customization we construct BoundGemm directly (the pattern
    // when axes diverge from the universal mint defaults).
    BoundGemm bound{scalar_gemm_ref};

    // Exercise the binding — the value() accessor returns the
    // wrapped function pointer; the call site is type-system-aware
    // of every per-axis grade above without paying a single byte
    // of runtime overhead.
    constexpr int M = 2, N = 2, K = 2;
    const float a[M * K] = { 1.f, 2.f,
                             3.f, 4.f };
    const float b[K * N] = { 5.f, 6.f,
                             7.f, 8.f };
    float c[M * N] = {};

    bound.value()(a, b, c, M, N, K);

    // Verify: C = [[1·5+2·7, 1·6+2·8], [3·5+4·7, 3·6+4·8]]
    //           = [[19, 22], [43, 50]]
    std::printf("custom_kernel result: [[%g, %g], [%g, %g]] "
                "(expected [[19, 22], [43, 50]])\n",
                static_cast<double>(c[0]),
                static_cast<double>(c[1]),
                static_cast<double>(c[2]),
                static_cast<double>(c[3]));

    // Echo the per-axis grades to stderr so a reviewer sees the
    // type-level metadata WITHOUT cracking open the source.
    std::printf("BoundGemm sizeof = %zu (== sizeof(GemmFp32Ptr) %zu)\n",
                sizeof(BoundGemm), sizeof(GemmFp32Ptr));
    return 0;
}
