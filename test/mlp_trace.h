#pragma once

// The shared part of test_mlp_trace: the op table of the perceptron, the
// builder of each op packet, and the steps of the run that drive the
// compiled replay.  The run up to the memory plan, and main, are in
// test_mlp_trace.cpp.

#include <crucible/Vigil.h>

#include <cstdint>

namespace test_mlp_trace {

// In a real frontend these hashes come from the operator schema.  The
// names in the trailing comments are the operators each one stands for.
inline constexpr crucible::SchemaHash OP_MM{0xA001};  // aten::mm
inline constexpr crucible::SchemaHash OP_ADD{0xA002};  // aten::add.Tensor
inline constexpr crucible::SchemaHash OP_RELU{0xA003};  // aten::relu
inline constexpr crucible::SchemaHash OP_LOSS_BWD{0xA004};  // aten::mse_loss_backward
inline constexpr crucible::SchemaHash OP_RELU_BWD{0xA005};  // aten::threshold_backward
inline constexpr crucible::SchemaHash OP_MM_BWD{0xA006};  // aten::mm (backward uses same op, different shapes)
inline constexpr crucible::SchemaHash OP_ACCUM_GRAD{0xA007};  // aten::add_ (gradient accumulation)

// The shape hash fingerprints the input geometry, so two positions that
// run the same operator on different shapes stay distinguishable.  The
// trailing comments on each row give the shapes the hash stands for,
// since the hash itself is opaque.
struct MlpOp {
    const char* name;
    crucible::SchemaHash schema_hash;
    crucible::ShapeHash shape_hash;
    uint16_t num_inputs;
    uint16_t num_outputs;
};

inline constexpr uint32_t NUM_OPS = 10;

inline constexpr MlpOp MLP_OPS[NUM_OPS] = {
    // Forward.
    {.name = "mm(input, W1)",
     .schema_hash = OP_MM,
     .shape_hash = crucible::ShapeHash{0xF001},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,8]×[8,4] → [4,4]
    {.name = "add(hidden, B1)",
     .schema_hash = OP_ADD,
     .shape_hash = crucible::ShapeHash{0xF002},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,4]+[4] → [4,4]
    {.name = "relu(biased)",
     .schema_hash = OP_RELU,
     .shape_hash = crucible::ShapeHash{0xF003},
     .num_inputs = 1,
     .num_outputs = 1},  // [4,4] → [4,4]
    {.name = "mm(act, W2)",
     .schema_hash = OP_MM,
     .shape_hash = crucible::ShapeHash{0xF004},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,4]×[4,2] → [4,2]
    {.name = "add(logits, B2)",
     .schema_hash = OP_ADD,
     .shape_hash = crucible::ShapeHash{0xF005},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,2]+[2] → [4,2]
    // Backward.
    {.name = "loss_bwd(output)",
     .schema_hash = OP_LOSS_BWD,
     .shape_hash = crucible::ShapeHash{0xF006},
     .num_inputs = 1,
     .num_outputs = 1},  // [4,2] → [4,2]
    {.name = "mm(grad, W2.T)",
     .schema_hash = OP_MM_BWD,
     .shape_hash = crucible::ShapeHash{0xF007},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,2]×[2,4] → [4,4]
    {.name = "relu_bwd(grad, act)",
     .schema_hash = OP_RELU_BWD,
     .shape_hash = crucible::ShapeHash{0xF008},
     .num_inputs = 2,
     .num_outputs = 1},  // [4,4]×[4,4] → [4,4]
    {.name = "mm(input.T, grad)",
     .schema_hash = OP_MM_BWD,
     .shape_hash = crucible::ShapeHash{0xF009},
     .num_inputs = 2,
     .num_outputs = 1},  // [8,4]×[4,4] → [8,4] (dW1)
    {.name = "accum_grad(W1, dW1)",
     .schema_hash = OP_ACCUM_GRAD,
     .shape_hash = crucible::ShapeHash{0xF00A},
     .num_inputs = 2,
     .num_outputs = 1},  // [8,4]+[8,4] → [8,4]
};

inline constexpr int64_t BATCH = 4, IN_DIM = 8, HIDDEN = 4, OUT_DIM = 2;

// What a frontend assembles from its live tensors before dispatching an
// op.  Three slots is enough for every op here: at most two inputs and
// one output.
struct OpPacket {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[3]{};
    uint16_t n_metas = 0;
};

// The op packets (test_mlp_trace_ops.cpp).
OpPacket build_op(uint32_t iter, uint32_t op_idx);
void feed_iteration(crucible::Vigil& vigil, uint32_t iter);

// Just enough of an iteration for the detector to confirm the boundary
// it has already provisionally found.
void feed_trigger(crucible::Vigil& vigil, uint32_t iter);

// The compiled replay (test_mlp_trace_replay.cpp): the activation, three
// compiled iterations, and the check of the data flow.
void activate_compiled_mode(crucible::Vigil& vigil);
void run_compiled_iterations(crucible::Vigil& vigil);
void verify_data_flow(crucible::Vigil& vigil);

}  // namespace test_mlp_trace
