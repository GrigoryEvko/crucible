#pragma once

// The shared part of test_vit: the dimensions of the mini vision
// transformer, the builder of each op packet, and the steps of the run that
// drive the compiled replay.  The run up to the memory plan, and main, are
// in test_vit.cpp.

#include <crucible/Vigil.h>

#include <cstdint>

namespace test_vit {

// Batch, patch count, hidden width, mlp width, class count.
inline constexpr int64_t B = 2, SEQ = 4, D = 16, MLP = 32, CL = 10;

// 19 forward ops and 11 backward ops (test_vit_ops.cpp).
inline constexpr uint32_t NUM_OPS = 30;

struct OpPacket {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[6]{};
    uint16_t n_metas = 0;
};

// The op packets (test_vit_ops.cpp).
OpPacket build_op(uint32_t op_idx, uint32_t iter);
void feed_iteration(crucible::Vigil& v, uint32_t iter);
void feed_trigger(crucible::Vigil& v, uint32_t iter);

// The compiled replay (test_vit_replay.cpp): the alignment, a thousand
// compiled iterations, and the check of the data flow from the attention
// to the output projection.  run_compiled_iterations returns the time of
// one dispatch in nanoseconds.
void align_and_complete_iteration(crucible::Vigil& vigil);
double run_compiled_iterations(crucible::Vigil& vigil);
void verify_attention_data_flow(crucible::Vigil& vigil);

}  // namespace test_vit
