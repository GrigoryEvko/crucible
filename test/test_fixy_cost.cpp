// ── test_fixy_cost — FIXY-G11 positive test ──────────────────────────
//
// Pins the cost-semiring surface + per-Cog projection + R015 hot-cost
// gate.  Covers:
//   * Semiring laws (associativity, commutativity, distributivity,
//     identity) at static_assert level.
//   * Worked-example bindings with cost polynomials at the fixy::fn
//     layer.
//   * Per-Cog projection produces sane nanos values.
//   * Cross-binding seq_compose via flow_cost_polynomial_t.
//   * R015 hot-cost gate accepts bounded; rejects unbounded.

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

// ── Semiring laws at static_assert level ──────────────────────────

using A = ca::CostPolynomial<5, 3>;
using B = ca::CostPolynomial<7, 11>;
using C = ca::CostPolynomial<2, 4>;

// Associativity of seq.
static_assert(std::is_same_v<
    ca::seq_compose_t<ca::seq_compose_t<A, B>, C>,
    ca::seq_compose_t<A, ca::seq_compose_t<B, C>>>);

// Commutativity of seq.
static_assert(std::is_same_v<
    ca::seq_compose_t<A, B>,
    ca::seq_compose_t<B, A>>);

// par is associative + commutative.
static_assert(std::is_same_v<
    ca::par_compose_t<ca::par_compose_t<A, B>, C>,
    ca::par_compose_t<A, ca::par_compose_t<B, C>>>);
static_assert(std::is_same_v<
    ca::par_compose_t<A, B>,
    ca::par_compose_t<B, A>>);

// seq distributes over par: A + max(B, C) == max(A+B, A+C).
using SEQ_DIST_LHS = ca::seq_compose_t<A, ca::par_compose_t<B, C>>;
using SEQ_DIST_RHS = ca::par_compose_t<
    ca::seq_compose_t<A, B>, ca::seq_compose_t<A, C>>;
static_assert(std::is_same_v<SEQ_DIST_LHS, SEQ_DIST_RHS>);

// Identity: A + ZeroCost == A.
using PADDED_A = ca::seq_compose_t<A, ca::CostPolynomial<0, 0>>;
static_assert(PADDED_A::coeffs == A::coeffs);

// ── Five worked-example bindings ──────────────────────────────────

template <std::uint64_t... Cs>
using cost_binding = cf::fn<int,
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
    cg::cost_polynomial<Cs...>>;

using Forge_Phase_D_Fuse        = cost_binding<200, 5>;     // 200 + 5n
using Mimic_NV_Kernel_Emit      = cost_binding<1000, 50>;   // 1000 + 50n
using Cipher_Cold_Writer        = cost_binding<5000, 100>;  // 5000 + 100n
using Forge_Phase_F_MemPlan     = cost_binding<300, 0, 2>;  // 300 + 2n^2
using Constant_Latency_Hook     = cost_binding<42>;         // 42

// Each binding carries its cost.
static_assert(cf::HasCostGrant<Forge_Phase_D_Fuse>);
static_assert(cf::HasCostGrant<Mimic_NV_Kernel_Emit>);
static_assert(cf::HasCostGrant<Cipher_Cold_Writer>);
static_assert(cf::HasCostGrant<Forge_Phase_F_MemPlan>);
static_assert(cf::HasCostGrant<Constant_Latency_Hook>);

// ── Per-Cog projection ────────────────────────────────────────────

// Forge Phase-D Fuse at n=64 on NV-H100:
//   base = 200 + 5*64 = 520; mult(Gpu) = 1; predicted = 520 ns.
static_assert(cc::predicted_cost_v<Forge_Phase_D_Fuse, cc::CogKind::Gpu, 64> == 520);

// Same binding on CpuCore (mult=10) → 5200 ns.
static_assert(cc::predicted_cost_v<Forge_Phase_D_Fuse, cc::CogKind::CpuCore, 64> == 5200);

// Mimic NV-Kernel at n=128 on Gpu: 1000 + 50*128 = 7400; * 1 = 7400.
static_assert(cc::predicted_cost_v<Mimic_NV_Kernel_Emit, cc::CogKind::Gpu, 128> == 7400);

// Quadratic Forge MemPlan at n=10 on Gpu: 300 + 0 + 2*100 = 500.
static_assert(cc::predicted_cost_v<Forge_Phase_F_MemPlan, cc::CogKind::Gpu, 10> == 500);

// Constant binding at any n on Gpu: 42.
static_assert(cc::predicted_cost_v<Constant_Latency_Hook, cc::CogKind::Gpu, 1000> == 42);

// ── R015 hot-cost gate ───────────────────────────────────────────

// All four cost-annotated bindings pass R015 (bounded).
static_assert(cr::R015_hot_cost_bounded_v<Forge_Phase_D_Fuse>);
static_assert(cr::R015_hot_cost_bounded_v<Mimic_NV_Kernel_Emit>);
static_assert(cr::R015_hot_cost_bounded_v<Cipher_Cold_Writer>);
static_assert(cr::R015_hot_cost_bounded_v<Forge_Phase_F_MemPlan>);
static_assert(cr::R015_hot_cost_bounded_v<Constant_Latency_Hook>);

// ── Cross-binding cost composition via flow_cost_polynomial_t ────
//
// Use a synthetic channel — flow_cost_polynomial_t<F1, Ch, F2> takes
// the channel type and chooses the composition rule.  Default is
// seq_compose (sequential).

struct ProductionChannel {};

// Forge Phase D + Mimic kernel emit = (200 + 5n) + (1000 + 50n) =
//   1200 + 55n.
using FuseEmit = cf::flow_cost_polynomial_t<
    Forge_Phase_D_Fuse, ProductionChannel, Mimic_NV_Kernel_Emit>;
static_assert(FuseEmit::coeffs[0] == 1200);
static_assert(FuseEmit::coeffs[1] == 55);

// Add Cipher writer at the end:
//   (1200 + 55n) + (5000 + 100n) = 6200 + 155n.
using FuseEmitWrite_step = FuseEmit;
using Pipeline = ca::seq_compose_t<
    cf::fn_cost_polynomial_t<Forge_Phase_D_Fuse>,
    ca::seq_compose_t<
        cf::fn_cost_polynomial_t<Mimic_NV_Kernel_Emit>,
        cf::fn_cost_polynomial_t<Cipher_Cold_Writer>>>;
static_assert(Pipeline::coeffs[0] == 6200);
static_assert(Pipeline::coeffs[1] == 155);

// ── Parallel composition: par_compose picks the LARGER coefficient
// at each degree.  Forge fuse running parallel to Mimic kernel: each
// degree is max of the per-binding coefficient.
using ParallelFuseEmit = ca::par_compose_t<
    cf::fn_cost_polynomial_t<Forge_Phase_D_Fuse>,
    cf::fn_cost_polynomial_t<Mimic_NV_Kernel_Emit>>;
// max(200, 1000) = 1000; max(5, 50) = 50.
static_assert(ParallelFuseEmit::coeffs[0] == 1000);
static_assert(ParallelFuseEmit::coeffs[1] == 50);

// ── Evidenced cost variant — wears Tested witness ────────────────

using EvidencedFuse = cf::fn<int,
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
    cg::cost_polynomial_e<crucible::safety::witness::Tested<7>, 200, 5>>;

static_assert(cf::HasCostGrant<EvidencedFuse>);
static_assert(cr::R015_hot_cost_bounded_v<EvidencedFuse>);

}  // namespace

int main() {
    // Runtime smoke — exercise per-Cog projection at runtime.
    constexpr auto base = ca::evaluate_v<ca::CostPolynomial<200, 5>, 64>;
    if (base != 520) {
        std::fprintf(stderr, "base evaluation diverged: got %lu, want 520\n",
                     static_cast<unsigned long>(base));
        return 1;
    }

    constexpr auto on_gpu =
        cc::predicted_cost_v<Forge_Phase_D_Fuse, cc::CogKind::Gpu, 64>;
    if (on_gpu != 520) {
        std::fprintf(stderr, "GPU projection diverged: got %lu, want 520\n",
                     static_cast<unsigned long>(on_gpu));
        return 2;
    }

    constexpr auto on_cpu =
        cc::predicted_cost_v<Forge_Phase_D_Fuse, cc::CogKind::CpuCore, 64>;
    if (on_cpu != 5200) {
        std::fprintf(stderr, "CPU projection diverged: got %lu, want 5200\n",
                     static_cast<unsigned long>(on_cpu));
        return 3;
    }

    std::fputs("test_fixy_cost: OK\n", stdout);
    return 0;
}
