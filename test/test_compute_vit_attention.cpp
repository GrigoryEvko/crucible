// Ops 0 to 6 of one compiled iteration of test_compute_vit: the attention
// block, computed through the pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using ::foundation::core::side::LeftFactor;
using ::foundation::core::side::RightFactor;
using cpu::make_dim;
using cpu::make_input;
using cpu::read_input;
using cpu::write_output;
namespace roles = cpu::side;

namespace test_compute_vit {

namespace {

// The op of a matrix product of [B*S, D] with [D, D].
void run_square_mm(CompiledScope& scope) {
    write_output(
        kTest, scope,
        [left = read_input(scope, InputPosition{0}), right = read_input(scope, InputPosition{1})](auto out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(left), make_input<RightFactor>(right), std::move(out),
                    make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D), make_dim<roles::Inner>(D));
        });
}

}  // namespace

void run_attention_ops(CompiledScope& scope) {
    ReplayStatus s;

    s = scope.advance(H_LN1, S_LN1).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [input = read_input(scope, InputPosition{0}), scale = read_input(scope, InputPosition{1}),
                  shift = read_input(scope, InputPosition{2})](auto out) noexcept {
                     cpu::layer_norm(kTest, input, make_input<roles::Scale>(scale), make_input<roles::Shift>(shift),
                                     std::move(out), make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
                 });

    s = scope.advance(H_MMQ, S_MMQ).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(scope);

    s = scope.advance(H_MMK, S_MMK).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(scope);

    s = scope.advance(H_MMV, S_MMV).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(scope);

    s = scope.advance(H_SDPA, S_SDPA).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [queries = read_input(scope, InputPosition{0}), keys = read_input(scope, InputPosition{1}),
                  values = read_input(scope, InputPosition{2})](auto out) noexcept {
                     cpu::sdpa(kTest, queries, make_input<roles::Keys>(keys), make_input<roles::Values>(values),
                               std::move(out), make_dim<roles::Batch>(B), make_dim<roles::Sequence>(S),
                               make_dim<roles::Width>(D));
                 });

    s = scope.advance(H_MMOUT, S_MMOUT).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(scope);

    s = scope.advance(H_ADD1, S_ADD1).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [augend = read_input(scope, InputPosition{0}),
                  addend = read_input(scope, InputPosition{1})](auto out) noexcept {
                     cpu::add(kTest, make_input<roles::Augend>(augend), make_input<roles::Addend>(addend),
                              std::move(out), make_dim<roles::Count>(B * S * D));
                 });
}

}  // namespace test_compute_vit
