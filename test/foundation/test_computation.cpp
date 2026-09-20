// Sentinel TU for Computation: both smoke tests, the size and wrapper
// pins, and the substrate view.

#include <foundation/algebra/GradedTrait.h>
#include <foundation/effects/Computation.h>

#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace fe = ::foundation::effects;
using fe::Effect;
using fe::Row;

// A Computation is exactly its payload, whatever the row names, and it
// satisfies the wrapper contract through its Graded base.
static_assert(sizeof(fe::Computation<Row<Effect::Bg>, int>) == sizeof(int));
static_assert(sizeof(fe::Computation<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg>, double>)
              == sizeof(double));
static_assert(fa::GradedWrapper<fe::Computation<Row<Effect::Bg>, int>>);
static_assert(fa::GradedWrapper<fe::Computation<Row<>, double>>);
static_assert(fa::IsGraded<fe::ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(!fa::IsGraded<fe::Computation<Row<Effect::Bg>, int>>,
              "the derived class is not itself a Graded specialization; its graded_type is");
static_assert(std::is_base_of_v<fe::ComputationGraded<Row<Effect::Bg>, int>, fe::Computation<Row<Effect::Bg>, int>>);
static_assert(fe::Computation<Row<Effect::Bg>, int>::modality == fa::ModalityKind::Relative);

}  // namespace

int main() {
    fe::runtime_smoke_test_computation_graded();
    fe::runtime_smoke_test_computation();

    int value = 21;  // deliberately not constexpr
    auto pure = fe::Computation<Row<>, int>::mint_computation(value);
    auto doubled = pure.map([](int x) { return x * 2; });
    if (doubled.extract() != 42) return 1;

    // The context carries the capability it claims (#172).
    fe::detail::exec_ctx_self_test::BgWitness bg_ctx{fe::testing::bg()};
    auto claimed = fe::Computation<Row<>, int>::mint_computation_in_ctx<Effect::Bg>(bg_ctx, value);
    auto wider = std::move(claimed).template weaken<Row<Effect::Bg, Effect::Alloc>>();
    if (wider.graded().peek() != 21) return 2;
    if (wider.grade() != decltype(wider)::grade_type{}) return 3;
    return 0;
}
