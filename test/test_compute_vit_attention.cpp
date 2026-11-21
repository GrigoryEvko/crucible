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
void run_square_mm(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::mm(kTest, make_input<LeftFactor>(read_input(ctx, cv, InputPosition{0})),
                make_input<RightFactor>(read_input(ctx, cv, InputPosition{1})), out, make_dim<roles::Rows>(B * S),
                make_dim<roles::Columns>(D), make_dim<roles::Inner>(D));
    });
}

}  // namespace

void run_attention_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN1, S_LN1, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::layer_norm(kTest, read_input(ctx, cv, InputPosition{0}),
                        make_input<roles::Scale>(read_input(ctx, cv, InputPosition{1})),
                        make_input<roles::Shift>(read_input(ctx, cv, InputPosition{2})), out,
                        make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
    });

    s = ctx.advance(H_MMQ, S_MMQ, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(ctx, cv);

    s = ctx.advance(H_MMK, S_MMK, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(ctx, cv);

    s = ctx.advance(H_MMV, S_MMV, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(ctx, cv);

    s = ctx.advance(H_SDPA, S_SDPA, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::sdpa(kTest, read_input(ctx, cv, InputPosition{0}),
                  make_input<roles::Keys>(read_input(ctx, cv, InputPosition{1})),
                  make_input<roles::Values>(read_input(ctx, cv, InputPosition{2})), out, make_dim<roles::Batch>(B),
                  make_dim<roles::Sequence>(S), make_dim<roles::Width>(D));
    });

    s = ctx.advance(H_MMOUT, S_MMOUT, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    run_square_mm(ctx, cv);

    s = ctx.advance(H_ADD1, S_ADD1, cv).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> out) noexcept {
        cpu::add(kTest, make_input<roles::Augend>(read_input(ctx, cv, InputPosition{0})),
                 make_input<roles::Addend>(read_input(ctx, cv, InputPosition{1})), out,
                 make_dim<roles::Count>(B * S * D));
    });
}

}  // namespace test_compute_vit
