// ════════════════════════════════════════════════════════════════════
// example_fixy_custom_optimizer — FIXY-E / Phase E worked example 1/4
//
// THE PATTERN: A USER-SUPPLIED Adam OPTIMIZER STEP, via fixy::fn
//
// Reject-by-default analogue of
//   examples/fn/example_custom_optimizer.cpp
// (the substrate-direct version using `safety::fn::Fn<...>`).
//
// SAME Adam (Kingma & Ba 2015) step:
//
//   m  = β₁·m + (1 - β₁)·∇L
//   v  = β₂·v + (1 - β₂)·∇L²
//   θ -= η · m̂ / (√v̂ + ε)
//
// DIFFERENT binding mechanism: every dim either RELAXED via `cg::*` or
// explicitly ACKNOWLEDGED-as-strict via `accept_default_strict_for<dim::X>`.
// 20 axes total; missing one is a compile error naming the unengaged
// dim (FixyNotEngaged_<DimName> diag).
//
// THE LOAD-BEARING DELTA from example_fixy_custom_kernel:
//   - Effect:      `cg::with<Bg, Alloc>` adds Alloc (kernel was Bg-only).
//   - Complexity:  `cg::complexity_linear<1>` declares O(1·N).
//   - Space:       `cg::space_bounded<3>` — three persistent buffers
//                  (params + m + v).
//   - Reentrancy:  STRICT (NonReentrant).  Optimizer state would race
//                  on m, v under concurrent calls — keeping the strict
//                  default is the LOAD-BEARING choice here, NOT a
//                  relaxation.  The strict-ack documents the choice
//                  explicitly so a reviewer can grep it.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cmath>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Trust-rationale tag ─────────────────────────────────────────────
//
// `grant::trust_assumed_for<TaintClass>` takes a typename rationale.
// The empty struct names the unverified-trust class for review/grep.
struct UserSuppliedOptimizer_NoFormalProof {};

// ── Adam step signature ─────────────────────────────────────────────

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

// ── fixy::fn binding — per-dim engagement choices ──────────────────

using BoundOptimizer = cf::fn<AdamStepPtr,
    // 1. Type — substrate carries AdamStepPtr.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True default.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable.
    cg::copy,

    // 4. Effect = Row<Bg, Alloc> — bg thread + may alloc scratch.
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,

    // 5. Security — optimizer sees user weights; Classified (strict)
    //    is correct.  fn::SecLevel::Internal would be slightly more
    //    accurate but isn't expressible in Phase B's fixy vocab
    //    (declassify → Public, upgrade_to_secret → Secret).
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime — free function, valid forever.
    cf::accept_default_strict_for<cd::Lifetime>,

    // 8. Provenance = from_source<FromUser> — user-authored optimizer.
    cg::from_source<::crucible::safety::source::FromUser>,

    // 9. Trust = trust_assumed_for<rationale> — user code, no formal
    //    proof.  Strict-default Verified would lie.
    cg::trust_assumed_for<UserSuppliedOptimizer_NoFormalProof>,

    // 10. Representation — function pointer is opaque addr.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Linear<1> — O(1·N) in parameter count.  The
    //     template arg is the per-element constant multiplier; the
    //     actual N (parameter count) is declared at the call site.
    cg::complexity_linear<1>,

    // 13. Precision = F32 — Adam accumulates in FP32.
    cg::precision_f32,

    // 14. Space = Bounded<3> — three persistent N-element buffers
    //     (params + m + v) per optimizer instance.
    cg::space_bounded<3>,

    // 15. Overflow — IEEE 754 FP overflow → infinity; Trap default.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation = Mutable — Adam updates params, m, v in place.
    cg::mutable_in_place,

    // 17. Reentrancy — STRICT (NonReentrant).  THIS IS THE LOAD-
    //     BEARING DELTA.  m and v are per-instance optimizer state;
    //     concurrent calls would race.  Keeping the strict default
    //     forbids `.value()` invocations from multiple threads, so
    //     a downstream consumer that pins Reentrant rejects this
    //     binding at the call site.
    cf::accept_default_strict_for<cd::Reentrancy>,

    // 18. Size — observation-depth N/A for a callable.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version — first revision; strict default 1.
    cf::accept_default_strict_for<cd::Version>,

    // 20. Staleness — no time decay on a function pointer.
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundOptimizer) == sizeof(AdamStepPtr),
    "EBO collapse failed — fixy::fn must remain byte-equivalent to its "
    "Type regardless of how many dim grants are engaged.");

// Per-axis routing — each relaxation actually flowed to the substrate.
static_assert(BoundOptimizer::usage_v       == fn::UsageMode::Copy);
static_assert(BoundOptimizer::reentrancy_v  == fn::ReentrancyMode::NonReentrant,
    "Optimizer must be NonReentrant — concurrent calls would race on m, v.");
static_assert(BoundOptimizer::mutation_v    == fn::MutationMode::Mutable);
static_assert(BoundOptimizer::security_v    == fn::SecLevel::Classified);
static_assert(std::is_same_v<typename BoundOptimizer::cost_t,
                             fn::cost::Linear<1>>,
    "Optimizer step is O(N) in parameter count.");
static_assert(std::is_same_v<typename BoundOptimizer::space_t,
                             fn::space::Bounded<3>>,
    "Optimizer holds exactly three N-sized persistent buffers.");
static_assert(std::is_same_v<typename BoundOptimizer::precision_t,
                             fn::precision::F32>);
static_assert(std::is_same_v<typename BoundOptimizer::source_t,
                             ::crucible::safety::source::FromUser>);
static_assert(std::is_same_v<typename BoundOptimizer::trust_t,
                             ::crucible::safety::trust::Unverified>);

// EffectRow contains BOTH Bg AND Alloc — the cross-cut signal the row
// algebra reads at composition sites.
static_assert(std::is_same_v<typename BoundOptimizer::effect_row_t,
                             fx::Row<fx::Effect::Bg, fx::Effect::Alloc>>);

}  // namespace

int main() {
    BoundOptimizer bound{adam_step_ref};

    constexpr int N = 4;
    float params[N] = { 1.0f, -0.5f, 2.0f, 0.0f };
    float m[N]      = {};
    float v[N]      = {};
    const float grad[N] = { 0.1f, -0.2f, 0.05f, 0.0f };

    bound.value()(params, m, v, grad, N,
                  /*lr=*/0.01f, /*beta1=*/0.9f, /*beta2=*/0.999f,
                  /*eps=*/1e-8f, /*step_t=*/1);

    std::printf("fixy custom_optimizer params after 1 step: "
                "[%g, %g, %g, %g]\n",
                static_cast<double>(params[0]),
                static_cast<double>(params[1]),
                static_cast<double>(params[2]),
                static_cast<double>(params[3]));

    std::printf("BoundOptimizer sizeof = %zu (== sizeof(AdamStepPtr) %zu) "
                "[20-dim grade vector, zero runtime cost]\n",
                sizeof(BoundOptimizer), sizeof(AdamStepPtr));
    return 0;
}
