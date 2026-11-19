// Ops 0 to 6 of one compiled iteration of test_compute_vit: the attention
// block, computed through the pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using crucible::test::host_ptr;

namespace test_compute_vit {

void run_attention_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN1, S_LN1, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::layer_norm(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
                    host_ptr<const float>(ctx.input_ptr(2, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D);

    s = ctx.advance(H_MMQ, S_MMQ, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMK, S_MMK, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMV, S_MMV, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_SDPA, S_SDPA, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::sdpa(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
              host_ptr<const float>(ctx.input_ptr(2, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B, S, D);

    s = ctx.advance(H_MMOUT, S_MMOUT, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_ADD1, S_ADD1, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::add(kTest, host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
             host_ptr<float>(ctx.output_ptr(0, cv)), B * S * D);
}

}  // namespace test_compute_vit
