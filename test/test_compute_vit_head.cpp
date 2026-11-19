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
#include "tensor_data.h"

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using crucible::test::host_ptr;

namespace test_compute_vit {

void run_ffn_and_head_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN2, S_LN2, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::layer_norm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
                    host_ptr<const float>(ctx.input_ptr(2, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D);

    s = ctx.advance(H_MMFF1, S_MMFF1, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D_FF, D);

    s = ctx.advance(H_RELU, S_RELU, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::relu(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B * S * D_FF);

    s = ctx.advance(H_MMFF2, S_MMFF2, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D_FF);

    s = ctx.advance(H_ADD2, S_ADD2, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::add(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
             host_ptr<float>(ctx.output_ptr(0, cv)), B * S * D);

    s = ctx.advance(H_IDXSEL, S_IDXSEL, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::index_select(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B, S, D, 0);

    s = ctx.advance(H_MMHEAD, S_MMHEAD, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B, N_CLS, D);

    s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv);
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
    cpu::softmax(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B, N_CLS);
}

}  // namespace test_compute_vit
