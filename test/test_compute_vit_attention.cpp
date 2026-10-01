// Ops 0 to 6 of one compiled iteration of test_compute_vit: the attention
// block, computed through the pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include "test_assert.h"

using namespace crucible;

namespace test_compute_vit {

void run_attention_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN1, S_LN1, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::layer_norm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
                    static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B * S,
                    D);

    s = ctx.advance(H_MMQ, S_MMQ, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMK, S_MMK, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMV, S_MMV, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_SDPA, S_SDPA, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::sdpa(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
              static_cast<const float*>(ctx.input_ptr(2, cv)), static_cast<float*>(ctx.output_ptr(0, cv)), B, S, D);

    s = ctx.advance(H_MMOUT, S_MMOUT, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
            static_cast<float*>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_ADD1, S_ADD1, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::add(static_cast<const float*>(ctx.input_ptr(0, cv)), static_cast<const float*>(ctx.input_ptr(1, cv)),
             static_cast<float*>(ctx.output_ptr(0, cv)), B * S * D);
}

}  // namespace test_compute_vit
