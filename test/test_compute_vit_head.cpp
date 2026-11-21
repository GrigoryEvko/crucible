// Ops 7 to 14 of one compiled iteration of test_compute_vit: the
// feed-forward block and the classification head, computed through the
// pool.

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

// The op of a matrix product of [rows, inner] with [inner, columns].
void run_mm(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv, cpu::Dim<roles::Rows> rows,
            cpu::Dim<roles::Columns> columns, cpu::Dim<roles::Inner> inner) {
    write_output(kTest, ctx, cv, [&ctx, &cv, &rows, &columns, &inner](::fixy::View<float> out) noexcept {
        cpu::mm(kTest, make_input<LeftFactor>(read_input(ctx, cv, InputPosition{0})),
                make_input<RightFactor>(read_input(ctx, cv, InputPosition{1})), out, rows, columns, inner);
    });
}

}  // namespace

void run_ffn_and_head_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN2, S_LN2, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::layer_norm(kTest, read_input(ctx, cv, InputPosition{0}),
                        make_input<roles::Scale>(read_input(ctx, cv, InputPosition{1})),
                        make_input<roles::Shift>(read_input(ctx, cv, InputPosition{2})), out,
                        make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
    });

    s = ctx.advance(H_MMFF1, S_MMFF1, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_mm(ctx, cv, make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D_FF), make_dim<roles::Inner>(D));

    s = ctx.advance(H_RELU, S_RELU, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::relu(kTest, read_input(ctx, cv, InputPosition{0}), out, make_dim<roles::Count>(B * S * D_FF));
    });

    s = ctx.advance(H_MMFF2, S_MMFF2, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_mm(ctx, cv, make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D), make_dim<roles::Inner>(D_FF));

    s = ctx.advance(H_ADD2, S_ADD2, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::add(kTest, make_input<roles::Augend>(read_input(ctx, cv, InputPosition{0})),
                 make_input<roles::Addend>(read_input(ctx, cv, InputPosition{1})), out,
                 make_dim<roles::Count>(B * S * D));
    });

    s = ctx.advance(H_IDXSEL, S_IDXSEL, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::index_select(kTest, read_input(ctx, cv, InputPosition{0}), out, make_dim<roles::Batch>(B),
                          make_dim<roles::Sequence>(S), make_dim<roles::Width>(D), make_dim<roles::Selected>(0));
    });

    s = ctx.advance(H_MMHEAD, S_MMHEAD, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_mm(ctx, cv, make_dim<roles::Rows>(B), make_dim<roles::Columns>(N_CLS), make_dim<roles::Inner>(D));

    s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::softmax(kTest, read_input(ctx, cv, InputPosition{0}), out, make_dim<roles::Rows>(B),
                     make_dim<roles::Width>(N_CLS));
    });
}

}  // namespace test_compute_vit
