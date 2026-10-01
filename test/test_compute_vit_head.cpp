// Ops 7 to 14 of one compiled iteration of test_compute_vit: the
// feed-forward block and the classification head, computed through the
// pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include "test_assert.h"

using namespace crucible;

namespace test_compute_vit {

void run_ffn_and_head_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN2, S_LN2, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::layer_norm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                    static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B * S,
                    D);

    s = ctx.advance(H_MMFF1, S_MMFF1, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D_FF, D);

    s = ctx.advance(H_RELU, S_RELU, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::relu(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)),
              B * S * D_FF);

    s = ctx.advance(H_MMFF2, S_MMFF2, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D_FF);

    s = ctx.advance(H_ADD2, S_ADD2, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::add(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
             static_cast<float*>(ctx.output_ptr(0, cv)), B * S * D);

    s = ctx.advance(H_IDXSEL, S_IDXSEL, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::index_select(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B, S,
                      D, 0);

    s = ctx.advance(H_MMHEAD, S_MMHEAD, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B, N_CLS, D);

    s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv);
    assert(s == ReplayStatus::COMPLETE);
    cpu::softmax(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B, N_CLS);
}

}  // namespace test_compute_vit
