// Ops 0 to 6 of one compiled iteration of test_compute_vit: the attention
// block, computed through the pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include "test_assert.h"
#include "tensor_data.h"

using namespace crucible;
using crucible::test::host_ptr;

namespace test_compute_vit {

void run_attention_ops(CrucibleContext& ctx, const CrucibleContext::CompiledView& cv) {
    ReplayStatus s;

    s = ctx.advance(H_LN1, S_LN1, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::layer_norm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
                    host_ptr<const float>(ctx.input_ptr(2, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D);

    s = ctx.advance(H_MMQ, S_MMQ, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMK, S_MMK, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_MMV, S_MMV, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_SDPA, S_SDPA, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::sdpa(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
              host_ptr<const float>(ctx.input_ptr(2, cv)), host_ptr<float>(ctx.output_ptr(0, cv)), B, S, D);

    s = ctx.advance(H_MMOUT, S_MMOUT, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::mm(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
            host_ptr<float>(ctx.output_ptr(0, cv)), B * S, D, D);

    s = ctx.advance(H_ADD1, S_ADD1, cv);
    assert(s == ReplayStatus::MATCH);
    cpu::add(host_ptr<const float>(ctx.input_ptr(0, cv)), host_ptr<const float>(ctx.input_ptr(1, cv)),
             host_ptr<float>(ctx.output_ptr(0, cv)), B * S * D);
}

}  // namespace test_compute_vit
