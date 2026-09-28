// example_custom_optimizer: a user optimizer step bound to the background
// thread.
//
// A user binds the step of their Adam optimizer (Kingma and Ba, 2015) to
// the training pipeline of Crucible.  The step is:
//
//   m  = b1 * m + (1 - b1) * g
//   v  = b2 * v + (1 - b2) * g^2
//   m' = m / (1 - b1^t)
//   v' = v / (1 - b2^t)
//   p -= lr * m' / (sqrt(v') + eps)
//
// The step updates three buffers in place, one element at a time: the
// parameters p, and the moments m and v.  The binding records that it runs on the background
// thread, that it can allocate scratch memory, that its cost is linear in
// the parameter count, and that it is not reentrant: the optimizer state
// changes on each call, so two calls at the same time race on m and v.
//
// The contrast with example_custom_kernel.cpp is in four axes.  The row
// adds Alloc.  The pack states a linear cost and three buffers.  The pack
// names no Reentrancy atom, so the step takes the strict pole, which
// refuses a self-call.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Role.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cmath>
#include <cstdio>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
using ::fixy::Axis;
using ::fixy::axis_traits;
using Effect = ::foundation::effects::Effect;

// One step over an array of n parameters.  In production p, m and v stay
// alive across calls as the running state of the optimizer, and g comes
// from the backward pass of each step.  lr, beta1, beta2 and eps are the
// hyperparameters, and step_t is the iteration count for the bias
// correction.
using AdamStepPtr = void (*)(float* params, float* m, float* v, const float* grad, int n, float lr, float beta1,
                             float beta2, float eps, int step_t) noexcept;

void adam_step_ref(float* params, float* m, float* v, const float* grad, int n, float lr, float beta1, float beta2,
                   float eps, int step_t) noexcept {
    const float bias1 = 1.f - std::pow(beta1, static_cast<float>(step_t));
    const float bias2 = 1.f - std::pow(beta2, static_cast<float>(step_t));
    for (int i = 0; i < n; ++i) {
        const float g = grad[i];
        m[i] = beta1 * m[i] + (1.f - beta1) * g;
        v[i] = beta2 * v[i] + (1.f - beta2) * g * g;
        const float m_hat = m[i] / bias1;
        const float v_hat = v[i] / bias2;
        params[i] -= lr * m_hat / (std::sqrt(v_hat) + eps);
    }
}

// The pack of a user optimizer step on the background thread, named one
// time.  fixy::mint_fn_for<UserBgOptimizer> is its door.
//
// The argument of cost_linear is the bound on N.  Zero states no bound,
// because the parameter count is not known here.  No rule reads the
// argument, and it is a part of the cache key.
template <class Step>
using UserBgOptimizer = ::fixy::fn<Step,
                                   at::copy,  // a function pointer is free to copy
                                   at::with<Effect::Bg, Effect::Alloc>,  // the background thread, and scratch memory
                                   at::as_public,  // the pointer holds no secret
                                   at::from_source<source::FromUser>,  // the user supplied the step
                                   at::trust_tested,  // the user has tests, and no proof
                                   at::cost_linear<0>,  // O(N) in the parameter count
                                   at::precision_f32,  // the step computes in FP32
                                   at::space_bounded<3>,  // three persistent buffers: p, m and v
                                   at::mut_mutable,  // the step updates the weights in place
                                   at::version<1>>;  // the first revision of this binding

using BoundOptimizer = UserBgOptimizer<AdamStepPtr>;

static_assert(sizeof(BoundOptimizer) == sizeof(AdamStepPtr), "a binding must be byte-equivalent to its payload");

// The axes that differ from the kernel binding.
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Reentrancy>, axis_traits<Axis::Reentrancy>::strict>,
              "the optimizer is not reentrant: two calls at the same time race on m and v");
static_assert(!BoundOptimizer::mentions_axis<Axis::Reentrancy>);
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Mutation>, at::mut_mutable>);
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Complexity>, at::cost_linear<0>>);
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Space>, at::space_bounded<3>>);

// The row holds Bg and Alloc, so a caller needs a context that admits the
// two.  The background drain context admits them, and the foreground
// context admits neither.  A row is a set, and binding_row_t gives it in
// the order of the enum.
static_assert(
    std::is_same_v<::fixy::binding_row_t<BoundOptimizer>, ::foundation::effects::Row<Effect::Alloc, Effect::Bg>>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, BoundOptimizer>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, BoundOptimizer>);

// The Effect grade and the Security grade are those of the role BgWorker
// in fixy/Role.h.  The step states more atoms than the role, so it names
// a pack of its own.
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Effect>,
                             ::fixy::role::BgWorker<AdamStepPtr>::grade_on<Axis::Effect>>);
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Security>,
                             ::fixy::role::BgWorker<AdamStepPtr>::grade_on<Axis::Security>>);

}  // namespace

int main() {
    const BoundOptimizer bound = ::fixy::mint_fn_for<UserBgOptimizer>(&adam_step_ref);

    // A model of four parameters, and one step of the optimizer.
    constexpr int N = 4;
    float params[N] = {1.0f, -0.5f, 2.0f, 0.0f};
    float m[N] = {};
    float v[N] = {};
    const float grad[N] = {0.1f, -0.2f, 0.05f, 0.0f};

    bound.value()(params, m, v, grad, N,
                  /*lr=*/0.01f, /*beta1=*/0.9f, /*beta2=*/0.999f,
                  /*eps=*/1e-8f, /*step_t=*/1);

    std::printf("custom_optimizer params after 1 step: [%g, %g, %g, %g]\n", static_cast<double>(params[0]),
                static_cast<double>(params[1]), static_cast<double>(params[2]), static_cast<double>(params[3]));

    // After the first step the bias correction gives m' = g and v' = g^2,
    // so each parameter with a gradient moves by lr against the sign of
    // its gradient.  The parameter with a zero gradient does not move.
    const float expected[N] = {0.99f, -0.49f, 1.99f, 0.0f};
    for (int i = 0; i < N; ++i) {
        if (std::fabs(params[i] - expected[i]) > 1e-5f) return 1;
    }

    std::printf("BoundOptimizer sizeof = %zu (== sizeof(AdamStepPtr) %zu)\n", sizeof(BoundOptimizer),
                sizeof(AdamStepPtr));
    return 0;
}
