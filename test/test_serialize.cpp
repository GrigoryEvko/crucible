#include <crucible/Serialize.h>
#include <crucible/Effects.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>

// Helper: build a minimal TensorMeta for testing.
static crucible::TensorMeta make_meta(int64_t size0, int64_t size1 = 0) {
    crucible::TensorMeta m{};
    if (size1 > 0) {
        m.ndim       = 2;
        m.sizes[0]   = size0;
        m.sizes[1]   = size1;
        m.strides[0] = size1;
        m.strides[1] = 1;
    } else {
        m.ndim       = 1;
        m.sizes[0]   = size0;
        m.strides[0] = 1;
    }
    m.dtype       = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx  = -1;
    m.layout      = crucible::Layout::Strided;
    m.data_ptr    = reinterpret_cast<void*>(0xDEADBEEF); // must become null on reload
    return m;
}

int main() {
    crucible::fx::Test test;
    crucible::Arena arena(1 << 16);

    // ── Build a 3-op RegionNode ─────────────────────────────────────
    // Op 0: schema=0xAA, 2 inputs (4×8 float, 4×8 float), 1 output (4×8 float)
    // Op 1: schema=0xBB, 2 inputs, 1 output
    // Op 2: schema=0xCC, 2 inputs, 1 output

    constexpr uint32_t NUM_OPS = 3;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(test.alloc, NUM_OPS);
    std::memset(ops, 0, NUM_OPS * sizeof(crucible::TraceEntry));

    // Set up metas: 2 inputs + 1 output per op.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        ops[i].schema_hash      = crucible::SchemaHash{0xAA + i * 0x11};
        ops[i].shape_hash       = crucible::ShapeHash{0x100 + i};
        ops[i].scope_hash       = crucible::ScopeHash{0x200 + i};
        ops[i].callsite_hash    = crucible::CallsiteHash{0x300 + i};
        ops[i].num_inputs       = 2;
        ops[i].num_outputs      = 1;
        ops[i].num_scalar_args  = 1;
        ops[i].grad_enabled     = (i % 2 == 0);
        // Vary op_flags across ops to exercise bit-packing round-trip.
        ops[i].inference_mode   = (i == 0);  // op 0: inference mode
        ops[i].is_mutable       = (i == 1);  // op 1: mutable (in-place)
        ops[i].training_phase   = static_cast<crucible::TrainingPhase>(i % 4);
        ops[i].torch_function   = (i == 2);  // op 2: torch_function active

        ops[i].input_metas      = arena.alloc_array<crucible::TensorMeta>(test.alloc, 2);
        ops[i].input_metas[0]   = make_meta(4, 8);
        ops[i].input_metas[1]   = make_meta(4, 8);

        ops[i].output_metas     = arena.alloc_array<crucible::TensorMeta>(test.alloc, 1);
        ops[i].output_metas[0]  = make_meta(4, 8);

        ops[i].scalar_args      = arena.alloc_array<int64_t>(test.alloc, 1);
        ops[i].scalar_args[0]   = static_cast<int64_t>(i * 42);

        ops[i].input_trace_indices  = arena.alloc_array<crucible::OpIndex>(test.alloc, 2);
        ops[i].input_trace_indices[0] = (i > 0) ? crucible::OpIndex{i - 1} : crucible::OpIndex{};
        ops[i].input_trace_indices[1] = crucible::OpIndex{};

        ops[i].input_slot_ids  = arena.alloc_array<crucible::SlotId>(test.alloc, 2);
        ops[i].input_slot_ids[0] = (i > 0) ? crucible::SlotId{(i - 1) * 10} : crucible::SlotId{};
        ops[i].input_slot_ids[1] = crucible::SlotId{};

        ops[i].output_slot_ids  = arena.alloc_array<crucible::SlotId>(test.alloc, 1);
        ops[i].output_slot_ids[0] = crucible::SlotId{i * 10};
    }

    // Build RegionNode.
    auto* region = crucible::make_region(test.alloc, arena, ops, NUM_OPS);
    assert(region != nullptr);
    assert(region->num_ops == NUM_OPS);

    const crucible::ContentHash original_content_hash = region->content_hash;
    const crucible::MerkleHash  original_merkle_hash  = region->merkle_hash;
    assert(static_cast<bool>(original_content_hash));

    // ── Serialize ───────────────────────────────────────────────────
    uint8_t buf[65536];
    const size_t n = crucible::serialize_region(region, nullptr, std::span<uint8_t>{buf, sizeof(buf)});
    assert(n > 0 && "serialize_region returned 0 — buffer too small or bad region");

    // ── Deserialize into a fresh arena ──────────────────────────────
    crucible::Arena arena2(1 << 16);
    crucible::RegionNode* loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, n}, arena2);
    assert(loaded != nullptr && "deserialize_region returned nullptr");

    // ── Verify round-trip ───────────────────────────────────────────
    assert(loaded->content_hash == original_content_hash);
    assert(loaded->merkle_hash  == original_merkle_hash);
    assert(loaded->num_ops      == NUM_OPS);
    assert(loaded->kind         == crucible::TraceNodeKind::REGION);

    // Schema hashes must round-trip exactly.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        assert(loaded->ops[i].schema_hash  == ops[i].schema_hash);
        assert(loaded->ops[i].shape_hash   == ops[i].shape_hash);
        assert(loaded->ops[i].scope_hash   == ops[i].scope_hash);
        assert(loaded->ops[i].num_inputs   == ops[i].num_inputs);
        assert(loaded->ops[i].num_outputs  == ops[i].num_outputs);
        assert(loaded->ops[i].num_scalar_args == ops[i].num_scalar_args);

        // op_flags fields must round-trip through bit-packed serialization.
        assert(loaded->ops[i].grad_enabled    == ops[i].grad_enabled);
        assert(loaded->ops[i].inference_mode  == ops[i].inference_mode);
        assert(loaded->ops[i].is_mutable      == ops[i].is_mutable);
        assert(loaded->ops[i].training_phase  == ops[i].training_phase);
        assert(loaded->ops[i].torch_function  == ops[i].torch_function);

        // data_ptr MUST be null after deserialization (not a meaningful address).
        for (uint16_t j = 0; j < ops[i].num_inputs; j++) {
            assert(loaded->ops[i].input_metas[j].data_ptr == nullptr
                   && "data_ptr must be null after deserialization");
            assert(loaded->ops[i].input_metas[j].ndim     == ops[i].input_metas[j].ndim);
            assert(loaded->ops[i].input_metas[j].dtype    == ops[i].input_metas[j].dtype);
            assert(loaded->ops[i].input_metas[j].sizes[0] == ops[i].input_metas[j].sizes[0]);
            assert(loaded->ops[i].input_metas[j].sizes[1] == ops[i].input_metas[j].sizes[1]);
            assert(loaded->ops[i].input_metas[j].strides[0] == ops[i].input_metas[j].strides[0]);
            assert(loaded->ops[i].input_metas[j].strides[1] == ops[i].input_metas[j].strides[1]);
        }
        for (uint16_t j = 0; j < ops[i].num_outputs; j++) {
            assert(loaded->ops[i].output_metas[j].data_ptr == nullptr);
        }

        // Scalar args round-trip.
        assert(loaded->ops[i].scalar_args[0] == ops[i].scalar_args[0]);

        // Trace indices + slot IDs round-trip.
        assert(loaded->ops[i].input_trace_indices[0]
               == ops[i].input_trace_indices[0]);
        assert(loaded->ops[i].input_slot_ids[0] == ops[i].input_slot_ids[0]);
        assert(loaded->ops[i].input_slot_ids[1] == ops[i].input_slot_ids[1]);
        assert(loaded->ops[i].output_slot_ids[0] == ops[i].output_slot_ids[0]);
    }

    // ── Verify content_hash is deterministic ────────────────────────
    // Recompute from the loaded ops — must match original.
    const crucible::ContentHash recomputed = crucible::compute_content_hash(
        std::span{loaded->ops, loaded->num_ops});
    assert(recomputed == original_content_hash);

    // ── Buffer-overflow safety: serializing into too-small a buffer ─
    uint8_t tiny_buf[4];
    const size_t n_tiny = crucible::serialize_region(region, nullptr, std::span<uint8_t>{tiny_buf, 4});
    assert(n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    // ── Corrupt-data safety: truncated buffer for deserialize ───────
    crucible::Arena arena3(1 << 16);
    crucible::RegionNode* bad = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, 10}, arena3);
    assert(bad == nullptr && "deserialize_region must return nullptr on truncated input");

    std::printf("test_serialize: all tests passed\n");
    return 0;
}
