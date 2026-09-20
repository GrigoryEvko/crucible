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

// Both bodies below were inline smoke tests in Computation.h, compiled
// into every translation unit that included it.  Both sit directly in
// foundation::effects rather than in a detail self-test namespace, so
// each takes one using-directive and its caller is renamed along with
// it.  The bodies move verbatim.

// Drives every accessor with non-constant arguments, where inline-body
// and constant-evaluation regressions surface that the assertions in
// the header cannot see.
void computation_graded_runs_at_run_time() noexcept {
    using namespace ::foundation::effects;
    using G_pure = ComputationGraded<Row<>, int>;
    using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

    int value_runtime = 42;
    G_pure pure{value_runtime, G_pure::grade_type{}};
    G_bg bg{value_runtime + 1, G_bg::grade_type{}};

    G_pure pure_default{};
    G_pure pure_copy{pure};
    G_pure pure_moved{std::move(pure_copy)};

    [[maybe_unused]] int const& peeked = pure.peek();
    [[maybe_unused]] int moved = std::move(pure_default).consume();
    [[maybe_unused]] auto grade = pure.grade();

    G_pure pure_a{};
    G_pure pure_b{};
    pure_a.peek_mut() = 7;
    pure_a.swap(pure_b);

    [[maybe_unused]] auto widened = std::move(bg).weaken(G_bg::grade_type{});

    G_pure pure_c{value_runtime, G_pure::grade_type{}};
    G_pure pure_d{value_runtime + 2, G_pure::grade_type{}};
    [[maybe_unused]] G_pure composed = pure_c.compose(pure_d);

    [[maybe_unused]] G_pure pure_bot = G_pure::at_bottom(value_runtime);
    [[maybe_unused]] G_bg bg_bot = G_bg::at_bottom(value_runtime + 3);

    static_assert(detail::computation_graded_caps::HasPeekMut<G_pure>);
    static_assert(detail::computation_graded_caps::HasWeaken<G_bg>);
    static_assert(!detail::computation_graded_caps::HasComonadExtract<G_pure>);
    static_assert(!detail::computation_graded_caps::HasRelMonadInject<G_bg>);
}

// Every operation is driven here with non-constant arguments.  The
// static_assert wall in the header only proves the constant-evaluated
// path, which does not exercise overload selection on real values.
void computation_runs_at_run_time() {
    using namespace ::foundation::effects;
    auto pure_lvalue = Computation<Row<>, int>::mint_computation(100);
    int read_lvalue = pure_lvalue.extract();
    int read_rvalue = std::move(pure_lvalue).extract();
    (void)read_lvalue;
    (void)read_rvalue;

    auto bg_pure = Computation<Row<>, int>::lift<Effect::Bg>(200);
    static_assert(std::is_same_v<decltype(bg_pure), Computation<Row<Effect::Bg>, int>>);

    auto bg_widened_lvalue = bg_pure.template weaken<Row<Effect::Bg, Effect::Alloc>>();
    auto bg_widened_rvalue = std::move(bg_pure).template weaken<Row<Effect::Bg, Effect::IO>>();
    (void)bg_widened_lvalue;
    (void)bg_widened_rvalue;

    auto map_pure = Computation<Row<>, int>::mint_computation(7);
    auto map_lvalue = map_pure.map([](int x) { return x + 1; });
    auto map_rvalue = std::move(map_pure).map([](int x) { return x * 2; });
    (void)map_lvalue;
    (void)map_rvalue;

    auto then_pure = Computation<Row<>, int>::mint_computation(11);
    auto then_lvalue = then_pure.then([](int x) { return Computation<Row<>, int>::lift<Effect::Bg>(x + 100); });
    auto then_rvalue =
        std::move(then_pure).then([](int x) { return Computation<Row<>, int>::lift<Effect::IO>(x + 200); });
    static_assert(std::is_same_v<decltype(then_lvalue)::row_type, Row<Effect::Bg>>);
    static_assert(std::is_same_v<decltype(then_rvalue)::row_type, Row<Effect::IO>>);
    (void)then_lvalue;
    (void)then_rvalue;

    auto graded_lvalue_owner = Computation<Row<Effect::Bg>, int>{555};
    auto const& g_view = graded_lvalue_owner.graded();
    [[maybe_unused]] int peeked = g_view.peek();

    auto graded_rvalue_owner = Computation<Row<Effect::Bg>, int>{666};
    auto g_moved = std::move(graded_rvalue_owner).graded();
    [[maybe_unused]] int peeked_moved = g_moved.peek();
}

}  // namespace

int main() {
    computation_graded_runs_at_run_time();
    computation_runs_at_run_time();

    int value = 21;  // deliberately not constexpr
    auto pure = fe::Computation<Row<>, int>::mint_computation(value);
    auto doubled = pure.map([](int x) { return x * 2; });
    if (doubled.extract() != 42) return 1;

    // The context carries the capability it claims.
    fe::detail::ctx_witnesses::BgWitness bg_ctx{fe::testing::bg()};
    auto claimed = fe::Computation<Row<>, int>::mint_computation_in_ctx<Effect::Bg>(bg_ctx, value);
    auto wider = std::move(claimed).template weaken<Row<Effect::Bg, Effect::Alloc>>();
    if (wider.graded().peek() != 21) return 2;
    if (wider.grade() != decltype(wider)::grade_type{}) return 3;
    return 0;
}
