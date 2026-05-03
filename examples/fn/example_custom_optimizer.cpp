// ════════════════════════════════════════════════════════════════════
// example_custom_optimizer — Phase 0 P0-5 / 5 (#1098)
//
// THE PATTERN: A USER-SUPPLIED OPTIMIZER STEP FUNCTION
//
// A user wants to bind their Adam (Kingma & Ba, 2015) optimizer
// step to Crucible's training pipeline.  The step:
//
//   m  = β₁·m + (1 - β₁)·∇L
//   v  = β₂·v + (1 - β₂)·∇L²
//   m̂  = m / (1 - β₁ᵗ)
//   v̂  = v / (1 - β₂ᵗ)
//   θ -= η · m̂ / (√v̂ + ε)
//
// Three buffers (params, m, v) updated in place per parameter.
// The substrate must capture: bg-thread execution, in-place mutation
// across N-element arrays, linear cost in N, allocation row (some
// optimizers keep scratch buffers), AND the non-reentrancy that
// follows from optimizer state being mutable across calls.
//
// THIS FILE: contrasts with example_custom_kernel.cpp by ADDING
// `Effect::Alloc` to the row, declaring `Cost::Linear<N>`, and
// pinning `Reentrancy::NonReentrant`.  Same callable-binding shape;
// different per-axis grade choices because the function's behavior
// differs.
// ════════════════════════════════════════════════════════════════════

#include <crucible/safety/Fn.h>

#include <cmath>
#include <cstdio>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Adam step signature ─────────────────────────────────────────────
//
// A single step over an N-element parameter array.  In production
// `params`, `m`, `v` are persistent across calls (the optimizer's
// running state); `grad` is per-step (computed by backward).
// `lr`, `beta1`, `beta2`, `eps` are hyperparameters; `step_t` is
// the iteration counter (for bias correction).

using AdamStepPtr = void(*)(float*       params,
                            float*       m,
                            float*       v,
                            const float* grad,
                            int          n,
                            float        lr,
                            float        beta1,
                            float        beta2,
                            float        eps,
                            int          step_t);

void adam_step_ref(float* params, float* m, float* v,
                   const float* grad, int n,
                   float lr, float beta1, float beta2, float eps,
                   int step_t) noexcept {
    const float bias1 = 1.f - std::pow(beta1, static_cast<float>(step_t));
    const float bias2 = 1.f - std::pow(beta2, static_cast<float>(step_t));
    for (int i = 0; i < n; ++i) {
        const float g = grad[i];
        m[i]      = beta1 * m[i] + (1.f - beta1) * g;
        v[i]      = beta2 * v[i] + (1.f - beta2) * g * g;
        const float m_hat = m[i] / bias1;
        const float v_hat = v[i] / bias2;
        params[i] -= lr * m_hat / (std::sqrt(v_hat) + eps);
    }
}

// ── The Fn<...> binding ────────────────────────────────────────────
//
// Per-axis grade choices for "user-supplied Adam optimizer step":
//
//   Type        : AdamStepPtr                       — function pointer
//   Refinement  : pred::True
//   Usage       : Copy                              — fn ptr is freely copyable
//   EffectRow   : Row<Bg, Alloc>                    — bg thread + may alloc scratch
//   Security    : SecLevel::Internal
//   Protocol    : proto::None
//   Lifetime    : lifetime::Static
//   Source      : source::FromUser
//   Trust       : trust::Tested
//   Repr        : ReprKind::Opaque
//   Cost        : cost::Linear<0>                   — O(N) per call (template param
//                                                     would be the per-call N upper
//                                                     bound; 0 = unspecified bound,
//                                                     but axis declared linear)
//   Precision   : precision::F32
//   Space       : space::Bounded<3>                 — three persistent buffers
//                                                     (params, m, v) per N elements
//   Overflow    : OverflowMode::Trap
//   Mutation    : MutationMode::Mutable             — in-place weight update
//   Reentrancy  : ReentrancyMode::NonReentrant      — single optimizer per training
//                                                     run; concurrent calls would
//                                                     race on m, v
//   Size        : size_pol::Unstated
//   Version     : 1
//   Staleness   : stale::Fresh
//
// The DELTA from example_custom_kernel.cpp is concentrated in 4 axes:
// EffectRow (adds Alloc), Cost (declares Linear), Mutation (stays
// Mutable), and Reentrancy (FLIPS to NonReentrant — optimizer state
// is per-instance and cannot be shared across threads).

using BoundOptimizer = fn::Fn<
    AdamStepPtr,                                // 1 Type
    fn::pred::True,                             // 2 Refinement
    fn::UsageMode::Copy,                        // 3 Usage
    fx::Row<fx::Effect::Bg, fx::Effect::Alloc>, // 4 EffectRow
    fn::SecLevel::Internal,                     // 5 Security
    fn::proto::None,                            // 6 Protocol
    fn::lifetime::Static,                       // 7 Lifetime
    fn::source::FromUser,                       // 8 Source
    fn::trust::Tested,                          // 9 Trust
    fn::ReprKind::Opaque,                       // 10 Repr
    fn::cost::Linear<0>,                        // 11 Cost — O(N) per step
    fn::precision::F32,                         // 12 Precision
    fn::space::Bounded<3>,                      // 13 Space — params + m + v
    fn::OverflowMode::Trap,                     // 14 Overflow
    fn::MutationMode::Mutable,                  // 15 Mutation
    fn::ReentrancyMode::NonReentrant,           // 16 Reentrancy
    fn::size_pol::Unstated,                     // 17 Size
    /*Version=*/1,                              // 18 Version
    fn::stale::Fresh                            // 19 Staleness
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundOptimizer) == sizeof(AdamStepPtr),
    "EBO collapse failed for optimizer binding.");

// Spot-check the axes that DIFFER from the kernel binding to make
// the contrast surface visible to a reviewer.
static_assert(BoundOptimizer::reentrancy_v == fn::ReentrancyMode::NonReentrant,
    "Optimizer must be NonReentrant — concurrent calls would race on m, v.");
static_assert(BoundOptimizer::mutation_v   == fn::MutationMode::Mutable,
    "Optimizer mutates params, m, v in place.");
static_assert(std::is_same_v<BoundOptimizer::cost_t, fn::cost::Linear<0>>,
    "Optimizer step is O(N) in parameter count.");
static_assert(std::is_same_v<BoundOptimizer::space_t, fn::space::Bounded<3>>,
    "Optimizer holds exactly three N-sized persistent buffers.");

// EffectRow contains BOTH Bg AND Alloc — this is the cross-cut signal
// the row algebra uses at composition sites (a caller that wants to
// invoke this step needs a context that admits both effects).
static_assert(std::is_same_v<BoundOptimizer::effect_row_t,
                             fx::Row<fx::Effect::Bg, fx::Effect::Alloc>>);

}  // namespace

int main() {
    BoundOptimizer bound{adam_step_ref};

    // Toy 4-parameter "model" with one optimizer step.
    constexpr int N = 4;
    float params[N] = { 1.0f, -0.5f, 2.0f, 0.0f };
    float m[N]      = {};
    float v[N]      = {};
    const float grad[N] = { 0.1f, -0.2f, 0.05f, 0.0f };

    bound.value()(params, m, v, grad, N,
                  /*lr=*/0.01f, /*beta1=*/0.9f, /*beta2=*/0.999f,
                  /*eps=*/1e-8f, /*step_t=*/1);

    std::printf("custom_optimizer params after 1 step: "
                "[%g, %g, %g, %g]\n",
                static_cast<double>(params[0]),
                static_cast<double>(params[1]),
                static_cast<double>(params[2]),
                static_cast<double>(params[3]));

    std::printf("BoundOptimizer sizeof = %zu (== sizeof(AdamStepPtr) %zu)\n",
                sizeof(BoundOptimizer), sizeof(AdamStepPtr));
    return 0;
}
