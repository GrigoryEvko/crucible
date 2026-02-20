// MLP training simulation through Crucible's dispatch pipeline.
//
// Demonstrates what happens when a Vessel adapter (PyTorch integration)
// feeds a 2-layer MLP forward+backward pass through Vigil::dispatch_op().
//
// Architecture:
//   input[4,8] → mm(weight1[8,4]) → relu → mm(weight2[4,2]) → output[4,2]
//   backward: grad flows back through the same chain
//
// The simulation shows:
//   1. First 2 iterations: RECORD mode — ops recorded, executed eagerly
//   2. Iteration boundary detected → BackgroundThread builds RegionNode
//   3. Activation via dispatch_op() → CrucibleContext enters COMPILED
//   4. Subsequent iterations: COMPILED mode — pre-allocated outputs,
//      data flows through the PoolAllocator without any per-op allocation
//
// This is what the Vessel adapter does for EVERY ATen op:
//   auto result = vigil.dispatch_op(entry, metas, n_metas);
//   if (result.action == COMPILED) {
//       // Kernel writes into vigil.output_ptr(j) — pre-allocated
//       execute_kernel(vigil.output_ptr(0), vigil.input_ptr(0), ...);
//   } else {
//       // Normal eager execution — PyTorch allocates output tensors
//       auto output = at::native::mm(input, weight);
//   }

#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

// ═══════════════════════════════════════════════════════════════════
// MLP op definitions — what ATen dispatches for a 2-layer MLP
// ═══════════════════════════════════════════════════════════════════

// Schema hashes (would be OperatorHandle::schema().hash() in real ATen)
static constexpr uint64_t OP_MM         = 0xA001; // aten::mm
static constexpr uint64_t OP_ADD        = 0xA002; // aten::add.Tensor
static constexpr uint64_t OP_RELU       = 0xA003; // aten::relu
static constexpr uint64_t OP_LOSS_BWD   = 0xA004; // aten::mse_loss_backward
static constexpr uint64_t OP_RELU_BWD   = 0xA005; // aten::threshold_backward
static constexpr uint64_t OP_MM_BWD     = 0xA006; // aten::mm (backward uses same op, different shapes)
static constexpr uint64_t OP_ACCUM_GRAD = 0xA007; // aten::add_ (gradient accumulation)

// Each op in the MLP iteration, declared statically.
// shape_hash is a fingerprint of input tensor geometry — unique per op position.
struct MlpOp {
    const char* name;       // human-readable
    uint64_t schema_hash;   // op identity
    uint64_t shape_hash;    // geometry fingerprint (unique per position)
    uint16_t num_inputs;    // how many tensor inputs
    uint16_t num_outputs;   // how many tensor outputs (always 1 here)
};

// 10 ops per iteration: 5 forward + 5 backward
static constexpr uint32_t NUM_OPS = 10;

static constexpr MlpOp MLP_OPS[NUM_OPS] = {
    // ── Forward pass ──
    {"mm(input, W1)",       OP_MM,         0xF001, 2, 1}, // [4,8]×[8,4] → [4,4]
    {"add(hidden, B1)",     OP_ADD,        0xF002, 2, 1}, // [4,4]+[4] → [4,4]
    {"relu(biased)",        OP_RELU,       0xF003, 1, 1}, // [4,4] → [4,4]
    {"mm(act, W2)",         OP_MM,         0xF004, 2, 1}, // [4,4]×[4,2] → [4,2]
    {"add(logits, B2)",     OP_ADD,        0xF005, 2, 1}, // [4,2]+[2] → [4,2]
    // ── Backward pass ──
    {"loss_bwd(output)",    OP_LOSS_BWD,   0xF006, 1, 1}, // [4,2] → [4,2]
    {"mm(grad, W2.T)",      OP_MM_BWD,     0xF007, 2, 1}, // [4,2]×[2,4] → [4,4]
    {"relu_bwd(grad, act)", OP_RELU_BWD,   0xF008, 2, 1}, // [4,4]×[4,4] → [4,4]
    {"mm(input.T, grad)",   OP_MM_BWD,     0xF009, 2, 1}, // [8,4]×[4,4] → [8,4] (dW1)
    {"accum_grad(W1, dW1)", OP_ACCUM_GRAD, 0xF00A, 2, 1}, // [8,4]+[8,4] → [8,4]
};

// ═══════════════════════════════════════════════════════════════════
// Tensor shapes for the MLP
// ═══════════════════════════════════════════════════════════════════

// Parameters (external tensors — persist across iterations)
static constexpr int64_t BATCH = 4, IN_DIM = 8, HIDDEN = 4, OUT_DIM = 2;

static TensorMeta make_2d(void* ptr, int64_t r, int64_t c) {
    TensorMeta m{};
    m.ndim = 2;
    m.sizes[0] = r; m.sizes[1] = c;
    m.strides[0] = c; m.strides[1] = 1;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.data_ptr = ptr;
    return m;
}

static TensorMeta make_1d(void* ptr, int64_t n) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = n;
    m.strides[0] = 1;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.data_ptr = ptr;
    return m;
}

// ═══════════════════════════════════════════════════════════════════
// Simulated tensor allocation
//
// In real PyTorch, tensors come from CUDACachingAllocator. Here we
// use stable fake pointers so the BackgroundThread can track data flow.
// Parameters get fixed addresses. Activations get per-iteration addresses.
// ═══════════════════════════════════════════════════════════════════

// Parameter pointers (stable across iterations — these are "external" to the pool)
static void* const W1_PTR = reinterpret_cast<void*>(uintptr_t(0x10000));
static void* const B1_PTR = reinterpret_cast<void*>(uintptr_t(0x20000));
static void* const W2_PTR = reinterpret_cast<void*>(uintptr_t(0x30000));
static void* const B2_PTR = reinterpret_cast<void*>(uintptr_t(0x40000));
static void* const X_PTR  = reinterpret_cast<void*>(uintptr_t(0x50000)); // input batch

// Activation pointer: unique per (iteration, tensor_id)
static void* act_ptr(uint32_t iter, uint32_t tensor_id) {
    return reinterpret_cast<void*>(
        uintptr_t((iter + 1) * 0x1000000 + (tensor_id + 1) * 0x10000));
}

// Activation tensor IDs (intermediates created during forward/backward)
enum Act : uint32_t {
    ACT_HIDDEN     = 0,  // mm output          [4,4]
    ACT_BIASED     = 1,  // add output         [4,4]
    ACT_ACTIVATED  = 2,  // relu output        [4,4]
    ACT_LOGITS     = 3,  // mm output          [4,2]
    ACT_OUTPUT     = 4,  // add output         [4,2]
    ACT_GRAD_OUT   = 5,  // loss_bwd output    [4,2]
    ACT_GRAD_ACT   = 6,  // mm_bwd output      [4,4]
    ACT_GRAD_BIA   = 7,  // relu_bwd output    [4,4]
    ACT_GRAD_W1    = 8,  // mm_bwd output      [8,4]
    ACT_W1_UPDATED = 9,  // accum_grad output  [8,4]
};

// ═══════════════════════════════════════════════════════════════════
// Build TensorMeta arrays for each op in the MLP iteration.
//
// This is what the Vessel adapter constructs from live PyTorch tensors
// before calling vigil.dispatch_op().
// ═══════════════════════════════════════════════════════════════════

struct OpPacket {
    TraceRing::Entry entry{};
    TensorMeta metas[3]{}; // max 2 inputs + 1 output = 3
    uint16_t n_metas = 0;
};

static OpPacket build_op(uint32_t iter, uint32_t op_idx) {
    OpPacket p;
    const auto& op = MLP_OPS[op_idx];

    p.entry.schema_hash = op.schema_hash;
    p.entry.shape_hash = op.shape_hash;
    p.entry.num_inputs = op.num_inputs;
    p.entry.num_outputs = op.num_outputs;

    uint16_t m = 0;

    // Build input TensorMetas based on op position.
    // The Vessel adapter reads these from live PyTorch tensors.
    switch (op_idx) {
    case 0: // mm(input, W1) → hidden
        p.metas[m++] = make_2d(X_PTR, BATCH, IN_DIM);        // input (external)
        p.metas[m++] = make_2d(W1_PTR, IN_DIM, HIDDEN);      // weight1 (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_HIDDEN), BATCH, HIDDEN);
        break;
    case 1: // add(hidden, B1) → biased
        p.metas[m++] = make_2d(act_ptr(iter, ACT_HIDDEN), BATCH, HIDDEN);
        p.metas[m++] = make_1d(B1_PTR, HIDDEN);               // bias1 (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);
        break;
    case 2: // relu(biased) → activated
        p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);
        p.metas[m++] = make_2d(act_ptr(iter, ACT_ACTIVATED), BATCH, HIDDEN);
        break;
    case 3: // mm(activated, W2) → logits
        p.metas[m++] = make_2d(act_ptr(iter, ACT_ACTIVATED), BATCH, HIDDEN);
        p.metas[m++] = make_2d(W2_PTR, HIDDEN, OUT_DIM);      // weight2 (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_LOGITS), BATCH, OUT_DIM);
        break;
    case 4: // add(logits, B2) → output
        p.metas[m++] = make_2d(act_ptr(iter, ACT_LOGITS), BATCH, OUT_DIM);
        p.metas[m++] = make_1d(B2_PTR, OUT_DIM);              // bias2 (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_OUTPUT), BATCH, OUT_DIM);
        break;
    case 5: // loss_bwd(output) → grad_out
        p.metas[m++] = make_2d(act_ptr(iter, ACT_OUTPUT), BATCH, OUT_DIM);
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_OUT), BATCH, OUT_DIM);
        break;
    case 6: // mm(grad_out, W2.T) → grad_act
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_OUT), BATCH, OUT_DIM);
        p.metas[m++] = make_2d(W2_PTR, HIDDEN, OUT_DIM);      // weight2 (external, transposed)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_ACT), BATCH, HIDDEN);
        break;
    case 7: // relu_bwd(grad_act, biased) → grad_biased
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_ACT), BATCH, HIDDEN);
        p.metas[m++] = make_2d(act_ptr(iter, ACT_BIASED), BATCH, HIDDEN);  // saved for backward
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_BIA), BATCH, HIDDEN);
        break;
    case 8: // mm(input.T, grad_biased) → grad_weight1
        p.metas[m++] = make_2d(X_PTR, BATCH, IN_DIM);         // input (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_BIA), BATCH, HIDDEN);
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_W1), IN_DIM, HIDDEN);
        break;
    case 9: // accum_grad(W1, grad_weight1) → W1_updated
        p.metas[m++] = make_2d(W1_PTR, IN_DIM, HIDDEN);       // weight1 (external)
        p.metas[m++] = make_2d(act_ptr(iter, ACT_GRAD_W1), IN_DIM, HIDDEN);
        p.metas[m++] = make_2d(act_ptr(iter, ACT_W1_UPDATED), IN_DIM, HIDDEN);
        break;
    }

    p.n_metas = m;
    return p;
}

// ═══════════════════════════════════════════════════════════════════
// Feed one complete MLP iteration via record_op (RECORDING mode).
// ═══════════════════════════════════════════════════════════════════

static void feed_iteration(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto pkt = build_op(iter, i);
        bool ok = vigil.record_op(pkt.entry, pkt.metas, pkt.n_metas);
        assert(ok && "record_op failed");
    }
}

// Feed first K ops (IterationDetector trigger).
static void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto pkt = build_op(iter, i);
        bool ok = vigil.record_op(pkt.entry, pkt.metas, pkt.n_metas);
        assert(ok);
    }
}

// Wait helpers.
static void wait_mode(Vigil& vigil) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(5000);
    while (!vigil.is_compiled()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ═══════════════════════════════════════════════════════════════════
// Main: simulate 5 training iterations through Crucible
// ═══════════════════════════════════════════════════════════════════

int main() {
    std::printf("═══ Crucible MLP Training Simulation ═══\n\n");
    std::printf("Model: input[%lld,%lld] -> mm -> relu -> mm -> output[%lld,%lld]\n",
                (long long)BATCH, (long long)IN_DIM,
                (long long)BATCH, (long long)OUT_DIM);
    std::printf("Ops per iteration: %u (5 forward + 5 backward)\n\n", NUM_OPS);

    Vigil vigil;

    // ── Phase 1: RECORDING (iterations 0-1) ────────────────────────
    //
    // The Vessel adapter calls vigil.record_op() for every op.
    // Ops execute eagerly via PyTorch's normal dispatch.
    // TraceRing accumulates the fingerprints; BackgroundThread watches.

    std::printf("── Iteration 0: RECORDING (building signature) ──\n");
    feed_iteration(vigil, 0);
    std::printf("   Recorded %u ops. Mode: RECORDING\n", NUM_OPS);

    std::printf("── Iteration 1: RECORDING (candidate match) ──\n");
    feed_iteration(vigil, 1);
    std::printf("   Recorded %u ops. Mode: RECORDING\n", NUM_OPS);

    // Feed K trigger ops from iteration 2 to confirm the boundary.
    // IterationDetector: signature built from iter 0, candidate at iter 1,
    // confirmed at iter 2's K-th matching op.
    std::printf("── Trigger: feeding first %u ops of iter 2 ──\n",
                IterationDetector::K);
    feed_trigger(vigil, 2);

    // Wait for background thread to process everything.
    vigil.flush();
    wait_mode(vigil);

    std::printf("\n   >>> Iteration boundary detected! <<<\n");
    std::printf("   BackgroundThread built RegionNode with %u ops\n",
                vigil.active_region()->num_ops);

    // ── Inspect the memory plan ──
    const auto* region = vigil.active_region();
    assert(region && region->plan);
    const auto* plan = region->plan;

    std::printf("\n── Memory Plan ──\n");
    std::printf("   Pool size: %llu bytes\n", (unsigned long long)plan->pool_bytes);
    std::printf("   Total slots: %u\n", plan->num_slots);
    std::printf("   External slots: %u (parameters)\n", plan->num_external);
    std::printf("   Internal slots: %u (activations — pre-allocated)\n",
                plan->num_slots - plan->num_external);

    std::printf("\n   Slot details:\n");
    for (uint32_t s = 0; s < plan->num_slots; s++) {
        const auto& slot = plan->slots[s];
        std::printf("     [%2u] %s  %6llu bytes  birth=op%u  death=op%u",
                    s,
                    slot.is_external ? "EXTERN" : "INTERN",
                    (unsigned long long)slot.nbytes,
                    slot.birth_op.raw(),
                    slot.death_op.raw());
        if (!slot.is_external)
            std::printf("  offset=%llu", (unsigned long long)slot.offset_bytes);
        std::printf("\n");
    }

    // ── Phase 2: Activate COMPILED mode via dispatch_op ────────────
    //
    // One dispatch_op call triggers try_activate_ (one-op delay).
    // After this, CrucibleContext has the PoolAllocator initialized
    // and ReplayEngine ready to walk the compiled region.

    std::printf("\n── Activating COMPILED mode ──\n");
    auto act_pkt = build_op(2, 0);
    auto act_result = vigil.dispatch_op(act_pkt.entry, act_pkt.metas, act_pkt.n_metas);
    assert(act_result.action == DispatchResult::Action::RECORD);
    assert(vigil.context().is_compiled());
    std::printf("   Activation op returned RECORD (one-op delay)\n");
    std::printf("   CrucibleContext: COMPILED, pool %llu bytes allocated\n",
                (unsigned long long)vigil.context().pool().pool_bytes());

    // ── Phase 3: COMPILED iterations ───────────────────────────────
    //
    // Every dispatch_op() now returns COMPILED. The Vessel adapter
    // uses output_ptr()/input_ptr() for pre-allocated storage.
    // No per-op allocation, no allocator contention.

    for (uint32_t iter = 3; iter <= 5; iter++) {
        std::printf("\n── Iteration %u: COMPILED ──\n", iter);

        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto pkt = build_op(iter, i);
            auto result = vigil.dispatch_op(pkt.entry, pkt.metas, pkt.n_metas);

            assert(result.action == DispatchResult::Action::COMPILED);

            const char* status_str =
                (result.status == ReplayStatus::MATCH)    ? "MATCH" :
                (result.status == ReplayStatus::COMPLETE)  ? "COMPLETE" :
                                                             "DIVERGED";

            // In COMPILED mode, output_ptr(0) is the pre-allocated buffer.
            // The Vessel adapter would execute the kernel writing here
            // instead of letting PyTorch allocate a new tensor.
            void* out = vigil.output_ptr(0);
            assert(out != nullptr);

            // Simulate writing to the output (in real code, this is the kernel).
            uint64_t tensor_bytes = 0;
            const auto& op = MLP_OPS[i];
            // Estimate output size from the op's output shape.
            // (In production, ReplayEngine knows exact sizes from TensorMeta.)
            switch (i) {
            case 0: case 1: case 2: case 7:     // [4,4] float
                tensor_bytes = BATCH * HIDDEN * 4; break;
            case 3: case 4: case 5: case 6:     // [4,2] float
                tensor_bytes = BATCH * OUT_DIM * 4; break;
            case 8: case 9:                      // [8,4] float
                tensor_bytes = IN_DIM * HIDDEN * 4; break;
            }
            std::memset(out, static_cast<int>(0x10 + i), tensor_bytes);

            if (i < 3 || i == NUM_OPS - 1) {
                std::printf("   op%2u %-25s → %s  out=%p\n",
                            i, op.name, status_str, out);
            } else if (i == 3) {
                std::printf("   ...  (ops 3-%u match)\n", NUM_OPS - 2);
            }
        }

        std::printf("   compiled_iterations=%u  diverged=%u\n",
                    vigil.compiled_iterations(), vigil.diverged_count());
    }

    // ── Phase 4: Verify data flow integrity ────────────────────────
    //
    // In the last compiled iteration, op 1's input should be op 0's output
    // (they share the same pool slot). This proves the DFG edges +
    // PoolAllocator + ReplayEngine wiring is correct.

    std::printf("\n── Data flow verification (iteration 6) ──\n");

    // Op 0: write known pattern to output
    auto p0 = build_op(6, 0);
    auto r0 = vigil.dispatch_op(p0.entry, p0.metas, p0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(0), 0xAB, BATCH * HIDDEN * 4);

    // Op 1: verify input carries op 0's output
    auto p1 = build_op(6, 1);
    auto r1 = vigil.dispatch_op(p1.entry, p1.metas, p1.n_metas);
    assert(r1.action == DispatchResult::Action::COMPILED);
    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
    bool flow_ok = true;
    for (uint32_t b = 0; b < BATCH * HIDDEN * 4; b++) {
        if (in_data[b] != 0xAB) { flow_ok = false; break; }
    }

    // Complete the iteration for clean state.
    for (uint32_t i = 2; i < NUM_OPS; i++) {
        auto pkt = build_op(6, i);
        (void)vigil.dispatch_op(pkt.entry, pkt.metas, pkt.n_metas);
    }

    std::printf("   op0 output → op1 input data flow: %s\n",
                flow_ok ? "VERIFIED" : "FAILED");
    assert(flow_ok);

    // ── Summary ────────────────────────────────────────────────────

    std::printf("\n═══ Summary ═══\n");
    std::printf("Total compiled iterations: %u\n", vigil.compiled_iterations());
    std::printf("Divergences: %u\n", vigil.diverged_count());
    std::printf("Pool bytes: %llu (vs eager: ~%llu per iteration)\n",
                (unsigned long long)plan->pool_bytes,
                (unsigned long long)(plan->num_slots - plan->num_external) *
                    BATCH * HIDDEN * 4);
    std::printf("Mode: %s\n",
                vigil.context().is_compiled() ? "COMPILED" : "RECORDING");
    std::printf("\ntest_mlp_trace: PASSED\n");
    return 0;
}
