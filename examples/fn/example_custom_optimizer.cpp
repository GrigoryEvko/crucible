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
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
namespace trust_level = ::fixy::tags::trust;
using ::fixy::Axis;
using ::fixy::strict_pole_t;
using Effect = ::foundation::effects::Effect;

// The roles of the four buffers of one step.  Each buffer is a Side of its
// role, so a call cannot swap two buffers of one element type.
namespace buffer_role {
struct Parameters final {};
struct Gradients final {};
struct FirstMoment final {};
struct SecondMoment final {};
}  // namespace buffer_role

using Parameters = ::fixy::Side<buffer_role::Parameters, ::fixy::StridedView<float, 1>>;
using Gradients = ::fixy::Side<buffer_role::Gradients, ::fixy::StridedView<float const, 1>>;
using FirstMoment = ::fixy::Side<buffer_role::FirstMoment, ::fixy::StridedView<float, 1>>;
using SecondMoment = ::fixy::Side<buffer_role::SecondMoment, ::fixy::StridedView<float, 1>>;

// The hyperparameters of one step, each with its name at the call.  step is
// the iteration count of the bias correction, from 1.
struct AdamHyperparameters {
    float learning_rate = 0.f;
    float beta1 = 0.f;
    float beta2 = 0.f;
    float epsilon = 0.f;
    std::uint32_t step = 0;
};

// One step over the parameters.  In production the parameters and the two
// moments stay alive across calls as the running state of the optimizer,
// and the gradients come from the backward pass of each step.  The count of
// the parameters comes from the views.
using AdamStepPtr = void (*)(Parameters const& params, FirstMoment const& m, SecondMoment const& v,
                             Gradients const& grad, AdamHyperparameters const& hyper) noexcept;

// The roles of the two operands of a power.  Each operand is a Side of its
// role, so a call states which number is the base and which is the exponent.
namespace power_role {
struct Base final {};
struct Exponent final {};
}  // namespace power_role

using PowerBase = ::fixy::Side<power_role::Base, float>;
using PowerExponent = ::fixy::Side<power_role::Exponent, std::uint32_t>;

// base to the power exponent, as exponent products in order.  The product
// calls no function of libm, so its bits are the same on each host.
// Complexity: O(exponent).
[[nodiscard]] float power_of(PowerBase const& base, PowerExponent const& exponent) noexcept {
    float result = 1.f;
    for (std::uint32_t turn = 0; turn < exponent.value(); ++turn) {
        result *= base.value();
    }
    return result;
}

// The step of each parameter, in the order of the parameters.  The
// precondition states that the four buffers hold one count.  Complexity:
// O(N) in the count of the parameters.
void adam_step_ref(Parameters const& params, FirstMoment const& m, SecondMoment const& v, Gradients const& grad,
                   AdamHyperparameters const& hyper) noexcept {
    std::size_t const count = params.value().count();
    CRUCIBLE_PRE(m.value().count() == count && v.value().count() == count && grad.value().count() == count);
    const PowerExponent step_count = ::fixy::side_as<power_role::Exponent>(hyper.step);
    const float bias1 = 1.f - power_of(::fixy::side_as<power_role::Base>(hyper.beta1), step_count);
    const float bias2 = 1.f - power_of(::fixy::side_as<power_role::Base>(hyper.beta2), step_count);
    auto const step = [&hyper, bias1, bias2](float& param, float& m_i, float& v_i, float const& g) noexcept {
        m_i = hyper.beta1 * m_i + (1.f - hyper.beta1) * g;
        v_i = hyper.beta2 * v_i + (1.f - hyper.beta2) * g * g;
        const float m_hat = m_i / bias1;
        const float v_hat = v_i / bias2;
        param -= hyper.learning_rate * m_hat / (::fixy::fp::sqrt(v_hat) + hyper.epsilon);
    };
    if (::fixy::walk(step, params.value(), m.value(), v.value(), grad.value()).is_err()) [[unlikely]] {
        ::fixy::fatal("adam_step_ref: the precondition holds, so the four buffers have one count");
    }
}

// The rank-1 view of the elements of run, in order.
template <class T, std::size_t Count>
[[nodiscard]] ::fixy::StridedView<T, 1> line_of(::fixy::View<T, Count> run) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{Count};
    ::fixy::FixedArray<std::size_t, 1> const strides{std::size_t{1}};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("a dense line fits its run");
}

// The unique rank-1 view of the elements of the run of a frame, in order.
// The view takes the run, so it is the one writer of the run.
template <class T, class Brand, std::size_t Count>
[[nodiscard]] ::fixy::StridedViewMut<T, 1, Brand> line_of(::fixy::ViewMut<T, Brand, Count> run) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{Count};
    ::fixy::FixedArray<std::size_t, 1> const strides{std::size_t{1}};
    return ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
        .expect("a dense line fits its run");
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
                                   at::trust<trust_level::Tested>,  // the user has tests, and no proof
                                   at::cost_linear<0>,  // O(N) in the parameter count
                                   at::precision_f32,  // the step computes in FP32
                                   at::space_bounded<3>,  // three persistent buffers: p, m and v
                                   at::mut_mutable,  // the step updates the weights in place
                                   at::version<1>>;  // the first revision of this binding

using BoundOptimizer = UserBgOptimizer<AdamStepPtr>;

static_assert(sizeof(BoundOptimizer) == sizeof(AdamStepPtr), "a binding must be byte-equivalent to its payload");

// The axes that differ from the kernel binding.
static_assert(std::is_same_v<BoundOptimizer::grade_on<Axis::Reentrancy>, strict_pole_t<Axis::Reentrancy>>,
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
    constexpr std::size_t N = 4;
    ::fixy::FixedArray<float, N> params{1.0f, -0.5f, 2.0f, 0.0f};
    ::fixy::FixedArray<float, N> m{};
    ::fixy::FixedArray<float, N> v{};
    const ::fixy::FixedArray<float, N> grad{0.1f, -0.2f, 0.05f, 0.0f};

    // The step writes the three state arrays through the runs that their frame doors lend.  The scope of
    // the unique view of each run gives the step the plain view that writes, for the call of the scope only.
    params.with_mut([&bound, &m, &v, &grad](auto params_run) noexcept {
        auto params_line = line_of(std::move(params_run));
        m.with_mut([&bound, &v, &grad, &params_line](auto first_moment_run) noexcept {
            auto first_moment_line = line_of(std::move(first_moment_run));
            v.with_mut([&bound, &grad, &params_line, &first_moment_line](auto second_moment_run) noexcept {
                auto second_moment_line = line_of(std::move(second_moment_run));
                static_cast<void>(::fixy::mint_strided_scope(params_line, [&](auto const params_scope) noexcept {
                    return ::fixy::mint_strided_scope(first_moment_line, [&](auto const first_moment_scope) noexcept {
                        return ::fixy::mint_strided_scope(
                            second_moment_line, [&](auto const second_moment_scope) noexcept {
                                bound.value()(::fixy::side_as<buffer_role::Parameters>(params_scope.view()),
                                              ::fixy::side_as<buffer_role::FirstMoment>(first_moment_scope.view()),
                                              ::fixy::side_as<buffer_role::SecondMoment>(second_moment_scope.view()),
                                              ::fixy::side_as<buffer_role::Gradients>(line_of(grad.view())),
                                              AdamHyperparameters{.learning_rate = 0.01f,
                                                                  .beta1 = 0.9f,
                                                                  .beta2 = 0.999f,
                                                                  .epsilon = 1e-8f,
                                                                  .step = 1});
                                return ::fixy::Unit{};
                            });
                    });
                }));
            });
        });
    });

    ::fixy::report(::fixy::Sink::Out, "custom_optimizer params after 1 step: [{}, {}, {}, {}]\n",
                   params.at(::fixy::index<0>()), params.at(::fixy::index<1>()), params.at(::fixy::index<2>()),
                   params.at(::fixy::index<3>()));

    // After the first step the bias correction gives m' = g and v' = g^2,
    // so each parameter with a gradient moves by lr against the sign of
    // its gradient.  The parameter with a zero gradient does not move.
    const ::fixy::FixedArray<float, N> expected{0.99f, -0.49f, 1.99f, 0.0f};
    for (::fixy::Index<N> const i : params.indices()) {
        if (::fixy::fp::abs(params.at(i) - expected.at(i)) > 1e-5f) return 1;
    }

    ::fixy::report(::fixy::Sink::Out, "BoundOptimizer sizeof = {} (== sizeof(AdamStepPtr) {})\n",
                   sizeof(BoundOptimizer), sizeof(AdamStepPtr));
    return 0;
}
