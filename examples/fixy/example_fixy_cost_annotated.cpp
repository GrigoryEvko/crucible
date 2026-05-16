// ── example_fixy_cost_annotated (FIXY-G11 worked example) ────────────
//
// Demonstrates dim::Cost engagement + per-Cog projection + cross-
// binding composition over the cost semiring.  Three production-shape
// bindings:
//
//   1. Forge Phase D FUSE      — pure IR transform, 200 + 5n ns base
//   2. Mimic NV kernel emit    — vendor backend kernel emit, 1000 + 50n ns
//   3. Cipher cold-tier writer — event-sourced persistence, 5000 + 100n ns
//
// Each binding's cost is annotated via `cg::cost_polynomial<...>`.
// The example:
//   - shows per-Cog projection (Gpu vs CpuCore differ by 10x)
//   - composes the three bindings sequentially via seq_compose
//   - composes Forge + Mimic in parallel via par_compose
//
// Compiles under the project's full warning + contract + reflection
// matrix; ctest entry exercises the runtime projection helpers.

#include <crucible/algebra/CostSemiring.h>
#include <crucible/cog/CostProjection.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Cost.h>

#include <cstdio>
#include <type_traits>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace ca = crucible::algebra;
namespace cc = crucible::cog;

namespace {

// ── Forge Phase D FUSE — pure IR transform ─────────────────────────

using ForgePhaseDFuse = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::cost_polynomial<200, 5>>;   // 200 + 5*n ns

// ── Mimic NV kernel emit ──────────────────────────────────────────

using MimicNvKernelEmit = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::cost_polynomial<1000, 50>>;   // 1000 + 50*n ns

// ── Cipher cold-tier writer ───────────────────────────────────────

using CipherColdWriter = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::cost_polynomial<5000, 100>>;   // 5000 + 100*n ns

// All three pass R015 (bounded cost).
static_assert(cr::R015_hot_cost_bounded_v<ForgePhaseDFuse>);
static_assert(cr::R015_hot_cost_bounded_v<MimicNvKernelEmit>);
static_assert(cr::R015_hot_cost_bounded_v<CipherColdWriter>);

// ── Cross-binding sequential composition ──────────────────────────
//
// (Forge + Mimic + Cipher) = (200 + 5n) + (1000 + 50n) + (5000 + 100n)
//                          = 6200 + 155n.

using Pipeline = ca::seq_compose_t<
    cf::fn_cost_polynomial_t<ForgePhaseDFuse>,
    ca::seq_compose_t<
        cf::fn_cost_polynomial_t<MimicNvKernelEmit>,
        cf::fn_cost_polynomial_t<CipherColdWriter>>>;

static_assert(Pipeline::coeffs[0] == 6200);
static_assert(Pipeline::coeffs[1] == 155);

// ── Parallel composition: max of per-degree coefficients ─────────

using ForgeMimicParallel = ca::par_compose_t<
    cf::fn_cost_polynomial_t<ForgePhaseDFuse>,
    cf::fn_cost_polynomial_t<MimicNvKernelEmit>>;
static_assert(ForgeMimicParallel::coeffs[0] == 1000);  // max(200, 1000)
static_assert(ForgeMimicParallel::coeffs[1] == 50);    // max(5, 50)

}  // namespace

int main() {
    // Per-Cog projection at concrete input sizes.
    constexpr std::uint64_t kInputSize = 64;

    constexpr std::uint64_t fuse_on_gpu =
        cc::predicted_cost_v<ForgePhaseDFuse, cc::CogKind::Gpu, kInputSize>;
    constexpr std::uint64_t fuse_on_cpu =
        cc::predicted_cost_v<ForgePhaseDFuse, cc::CogKind::CpuCore, kInputSize>;
    constexpr std::uint64_t emit_on_gpu =
        cc::predicted_cost_v<MimicNvKernelEmit, cc::CogKind::Gpu, kInputSize>;
    constexpr std::uint64_t cipher_on_cpu =
        cc::predicted_cost_v<CipherColdWriter, cc::CogKind::CpuCore, kInputSize>;

    constexpr std::uint64_t pipeline_on_gpu =
        ca::evaluate_v<Pipeline, kInputSize> *
        cc::cog_cost_multiplier_v<cc::CogKind::Gpu>;

    std::printf("Forge Phase D Fuse @ n=%lu:\n",
                static_cast<unsigned long>(kInputSize));
    std::printf("  on Gpu     = %lu ns\n", static_cast<unsigned long>(fuse_on_gpu));
    std::printf("  on CpuCore = %lu ns\n", static_cast<unsigned long>(fuse_on_cpu));
    std::printf("Mimic NV kernel emit @ n=%lu on Gpu = %lu ns\n",
                static_cast<unsigned long>(kInputSize),
                static_cast<unsigned long>(emit_on_gpu));
    std::printf("Cipher cold writer @ n=%lu on CpuCore = %lu ns\n",
                static_cast<unsigned long>(kInputSize),
                static_cast<unsigned long>(cipher_on_cpu));
    std::printf("Pipeline (seq compose) @ n=%lu on Gpu = %lu ns\n",
                static_cast<unsigned long>(kInputSize),
                static_cast<unsigned long>(pipeline_on_gpu));

    // Sanity: fuse_on_cpu == fuse_on_gpu * 10  (multiplier ratio).
    const std::uint64_t expected_ratio = fuse_on_gpu * std::uint64_t{10};
    if (fuse_on_cpu != expected_ratio) {
        std::fprintf(stderr,
            "Multiplier ratio diverged: cpu=%lu, gpu*10=%lu\n",
            static_cast<unsigned long>(fuse_on_cpu),
            static_cast<unsigned long>(expected_ratio));
        return 1;
    }

    std::fputs("example_fixy_cost_annotated: OK\n", stdout);
    return 0;
}
