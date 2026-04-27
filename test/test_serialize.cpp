#include <crucible/Serialize.h>
#include <crucible/effects/Capabilities.h>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <span>

// Family-A (persistent, cross-process stable) FNV-1a byte hash.
// Used below for wire-byte stability goldens — the bytes are the
// ground truth per the Types.h taxonomy, and any drift in this hash
// indicates a wire-format change (→ CDAG_VERSION bump required).
static uint64_t fnv1a_bytes(std::span<const uint8_t> bytes) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV offset basis
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ULL;            // FNV prime
    }
    return h;
}

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
    crucible::effects::Test test;
    crucible::Arena arena(1 << 16);

    // ── Build a 3-op RegionNode ─────────────────────────────────────
    // Op 0: schema=0xAA, 2 inputs (4×8 float, 4×8 float), 1 output (4×8 float)
    // Op 1: schema=0xBB, 2 inputs, 1 output
    // Op 2: schema=0xCC, 2 inputs, 1 output

    constexpr uint32_t NUM_OPS = 3;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);

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

    // ── §10.8 wire-stability: serialize twice, bytes identical ──────
    //
    // Catches uninit-memory-to-wire leaks that pass the structural
    // round-trip above but would corrupt Cipher under concurrent use
    // or across repeated snapshots of the same region.  Both calls
    // target fresh buffers so no prior-call state can bias the second.
    {
        uint8_t buf_a[65536];
        uint8_t buf_b[65536];
        std::memset(buf_a, 0xAA, sizeof(buf_a));  // poison pattern
        std::memset(buf_b, 0xBB, sizeof(buf_b));  // different poison

        const size_t na = crucible::serialize_region(region, nullptr,
            std::span<uint8_t>{buf_a, sizeof(buf_a)});
        const size_t nb = crucible::serialize_region(region, nullptr,
            std::span<uint8_t>{buf_b, sizeof(buf_b)});
        assert(na == nb && "serialize_region byte count must be deterministic");
        assert(na > 0);
        assert(std::memcmp(buf_a, buf_b, na) == 0
               && "serialize_region bytes must be deterministic — uninit-memory leak?");
    }

    // ── §10.8 wire-byte golden: Family-A cross-process stable hash ──
    //
    // Pins the canonical fixture's wire-byte hash.  Unlike the earlier
    // expr/cse-hash experiments (Family-B, ASLR-dependent), wire bytes
    // are cross-process stable BY CONSTRUCTION — data_ptr is zeroed
    // on write (see Serialize.h write_meta), padding is explicit,
    // strong-typed fields serialize as raw bytes.  Any drift here is
    // a wire-format change and requires a CDAG_VERSION bump in the
    // same commit that updates this golden.
    //
    // Two goldens are pinned: the byte length (catches size drift)
    // and the FNV-1a hash of all bytes (catches content drift within
    // a byte-stable size).  Both are anchored to CDAG_VERSION=8.
    {
        static constexpr uint32_t EXPECTED_CDAG_VERSION = 8;
        static_assert(crucible::CDAG_VERSION == EXPECTED_CDAG_VERSION,
            "CDAG_VERSION bump detected — update wire-byte golden below "
            "after confirming the new bytes hash to the expected value.");

        const uint64_t wire_hash = fnv1a_bytes(std::span<const uint8_t>{buf, n});

        // ── GOLDEN VALUES (update atomically with CDAG_VERSION bump) ──
        //
        //   To refresh after an intentional wire-format change:
        //     1. bump CDAG_VERSION in Serialize.h
        //     2. run this test once, capture the printed values
        //     3. update the two constants below
        //     4. commit all three in the same change
        constexpr size_t   EXPECTED_WIRE_BYTES = 1772;
        constexpr uint64_t EXPECTED_WIRE_HASH  = 0x88c4973fb04b16c5ULL;

        if (n != EXPECTED_WIRE_BYTES || wire_hash != EXPECTED_WIRE_HASH) {
            std::fprintf(stderr,
                "WIRE-FORMAT DRIFT DETECTED\n"
                "  got    n=%zu  hash=0x%016" PRIx64 "\n"
                "  expect n=%zu  hash=0x%016" PRIx64 "\n"
                "  if intentional: update EXPECTED_WIRE_BYTES and\n"
                "  EXPECTED_WIRE_HASH in test_serialize.cpp, and\n"
                "  verify CDAG_VERSION was bumped in Serialize.h.\n",
                n, wire_hash, EXPECTED_WIRE_BYTES, EXPECTED_WIRE_HASH);
            assert(false && "wire-byte golden mismatch");
        }
    }

    // ── Buffer-overflow safety: serializing into too-small a buffer ─
    uint8_t tiny_buf[4];
    const size_t n_tiny = crucible::serialize_region(region, nullptr, std::span<uint8_t>{tiny_buf, 4});
    assert(n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    // ── Corrupt-data safety: truncated buffer for deserialize ───────
    crucible::Arena arena3(1 << 16);
    crucible::RegionNode* bad = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, 10}, arena3);
    assert(bad == nullptr && "deserialize_region must return nullptr on truncated input");

    // ── Adversarial-header caps: reject before any giant allocation ──
    // Build a minimal header, then overwrite num_ops with 0xFFFFFFFF.
    // Pre-cap deserializer would allocate 4 G × sizeof(TraceEntry)
    // before failing; now rejects up front.
    {
        // Offset 0..3 = magic; 4..7 = version; 8 = kind byte; 9..15 pad;
        // 16..23 merkle; 24..31 content; 32..35 num_ops.  See Serialize.h.
        std::vector<uint8_t> adv(128, 0);
        const uint32_t magic = crucible::CDAG_MAGIC;
        const uint32_t version = crucible::CDAG_VERSION;
        const uint8_t  kind = static_cast<uint8_t>(crucible::TraceNodeKind::REGION);
        std::memcpy(adv.data() +  0, &magic,   4);
        std::memcpy(adv.data() +  4, &version, 4);
        adv[8] = kind;
        // merkle_hash + content_hash stay zero.
        const uint32_t bogus_num_ops = 0xFFFF'FFFFu;
        std::memcpy(adv.data() + 32, &bogus_num_ops, 4);

        crucible::Arena arena4(1 << 16);
        crucible::RegionNode* r = crucible::deserialize_region(
            test.alloc, std::span<const uint8_t>{adv.data(), adv.size()},
            arena4);
        assert(r == nullptr
               && "deserialize_region must reject num_ops > CDAG_MAX_OPS");
    }

    std::printf("test_serialize: all tests passed\n");
    return 0;
}
