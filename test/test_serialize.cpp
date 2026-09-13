#include <crucible/Serialize.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <bit>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <span>

// A drift in this hash means the wire format changed, which requires a
// CDAG_VERSION bump.
static uint64_t fnv1a_bytes(std::span<const uint8_t> bytes) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV offset basis
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ULL;  // FNV prime
    }
    return h;
}

static crucible::TensorMeta make_meta(int64_t size0, int64_t size1 = 0) {
    crucible::TensorMeta m{};
    if (size1 > 0) {
        m.ndim = 2;
        m.sizes[0] = ::crucible::tensor_dim(size0);
        m.sizes[1] = ::crucible::tensor_dim(size1);
        m.strides[0] = ::crucible::tensor_dim(size1);
        m.strides[1] = ::crucible::tensor_dim(1);
    } else {
        m.ndim = 1;
        m.sizes[0] = ::crucible::tensor_dim(size0);
        m.strides[0] = ::crucible::tensor_dim(1);
    }
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = -1;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(
        std::bit_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF)));  // must become null on reload
    m.grad_fn_hash = crucible::grad_fn_hash(0xA11CE000BADDF00DULL);
    return m;
}

[[gnu::cold]] int main() {
    auto test = crucible::effects::testing::test();
    crucible::Arena arena(1 << 16);

    constexpr uint32_t NUM_OPS = 3;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        ops[i].schema_hash = crucible::SchemaHash{0xAA + i * 0x11};
        ops[i].shape_hash = crucible::ShapeHash{0x100 + i};
        ops[i].scope_hash = crucible::ScopeHash{0x200 + i};
        ops[i].callsite_hash = crucible::CallsiteHash{0x300 + i};
        ops[i].num_inputs = 2;
        ops[i].num_outputs = 1;
        ops[i].num_scalar_args = 1;
        ops[i].grad_enabled = (i % 2 == 0);
        // Vary op_flags across ops to exercise bit-packing round-trip.
        ops[i].inference_mode = (i == 0);
        ops[i].is_mutable = (i == 1);
        ops[i].training_phase = static_cast<crucible::TrainingPhase>(i % 4);
        ops[i].torch_function = (i == 2);

        ops[i].input_metas = arena.alloc_array<crucible::TensorMeta>(test.alloc, 2);
        ops[i].input_metas[0] = make_meta(4, 8);
        ops[i].input_metas[1] = make_meta(4, 8);

        ops[i].output_metas = arena.alloc_array<crucible::TensorMeta>(test.alloc, 1);
        ops[i].output_metas[0] = make_meta(4, 8);

        ops[i].scalar_args = arena.alloc_array<int64_t>(test.alloc, 1);
        ops[i].scalar_args[0] = static_cast<int64_t>(i * 42);

        ops[i].input_trace_indices = arena.alloc_array<crucible::OpIndex>(test.alloc, 2);
        ops[i].input_trace_indices[0] = (i > 0) ? crucible::OpIndex{i - 1} : crucible::OpIndex{};
        ops[i].input_trace_indices[1] = crucible::OpIndex{};

        ops[i].input_slot_ids = arena.alloc_array<crucible::SlotId>(test.alloc, 2);
        ops[i].input_slot_ids[0] = (i > 0) ? crucible::SlotId{(i - 1) * 10} : crucible::SlotId{};
        ops[i].input_slot_ids[1] = crucible::SlotId{};

        ops[i].output_slot_ids = arena.alloc_array<crucible::SlotId>(test.alloc, 1);
        ops[i].output_slot_ids[0] = crucible::SlotId{i * 10};
    }

    auto* region = crucible::make_region(test.alloc, arena, ops, NUM_OPS);
    assert(region != nullptr);
    assert(region->num_ops == NUM_OPS);

    const crucible::ContentHash original_content_hash = region->content_hash;
    const crucible::MerkleHash original_merkle_hash = region->merkle_hash;
    assert(static_cast<bool>(original_content_hash));

    uint8_t buf[65536];
    const size_t n = crucible::serialize_region(region, nullptr, std::span<uint8_t>{buf, sizeof(buf)});
    assert(n > 0 && "serialize_region returned 0 — buffer too small or bad region");

    crucible::Arena arena2(1 << 16);
    auto loaded_region = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, n}, arena2);
    crucible::RegionNode* loaded = loaded_region.value();
    assert(loaded != nullptr && "deserialize_region returned nullptr");

    assert(loaded->content_hash == original_content_hash);
    assert(loaded->merkle_hash == original_merkle_hash);
    assert(loaded->num_ops == NUM_OPS);
    assert(loaded->kind == crucible::TraceNodeKind::REGION);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        assert(loaded->ops[i].schema_hash == ops[i].schema_hash);
        assert(loaded->ops[i].shape_hash == ops[i].shape_hash);
        assert(loaded->ops[i].scope_hash == ops[i].scope_hash);
        assert(loaded->ops[i].num_inputs == ops[i].num_inputs);
        assert(loaded->ops[i].num_outputs == ops[i].num_outputs);
        assert(loaded->ops[i].num_scalar_args == ops[i].num_scalar_args);

        // These five fields share one packed byte on the wire.
        assert(loaded->ops[i].grad_enabled == ops[i].grad_enabled);
        assert(loaded->ops[i].inference_mode == ops[i].inference_mode);
        assert(loaded->ops[i].is_mutable == ops[i].is_mutable);
        assert(loaded->ops[i].training_phase == ops[i].training_phase);
        assert(loaded->ops[i].torch_function == ops[i].torch_function);

        for (uint16_t j = 0; j < ops[i].num_inputs; j++) {
            assert(crucible::raw_data_ptr(loaded->ops[i].input_metas[j]) == nullptr
                   && "data_ptr must be null after deserialization");
            assert(crucible::raw_grad_fn_hash(ops[i].input_metas[j]) != 0
                   && "test fixture must exercise non-zero Family-B input");
            assert(crucible::raw_grad_fn_hash(loaded->ops[i].input_metas[j]) == 0
                   && "grad_fn_hash must be zero after deserialization");
            assert(loaded->ops[i].input_metas[j].ndim == ops[i].input_metas[j].ndim);
            assert(loaded->ops[i].input_metas[j].dtype == ops[i].input_metas[j].dtype);
            assert(crucible::raw_tensor_dim(loaded->ops[i].input_metas[j].sizes[0])
                   == crucible::raw_tensor_dim(ops[i].input_metas[j].sizes[0]));
            assert(crucible::raw_tensor_dim(loaded->ops[i].input_metas[j].sizes[1])
                   == crucible::raw_tensor_dim(ops[i].input_metas[j].sizes[1]));
            assert(crucible::raw_tensor_dim(loaded->ops[i].input_metas[j].strides[0])
                   == crucible::raw_tensor_dim(ops[i].input_metas[j].strides[0]));
            assert(crucible::raw_tensor_dim(loaded->ops[i].input_metas[j].strides[1])
                   == crucible::raw_tensor_dim(ops[i].input_metas[j].strides[1]));
        }
        for (uint16_t j = 0; j < ops[i].num_outputs; j++) {
            assert(crucible::raw_data_ptr(loaded->ops[i].output_metas[j]) == nullptr);
            assert(crucible::raw_grad_fn_hash(ops[i].output_metas[j]) != 0);
            assert(crucible::raw_grad_fn_hash(loaded->ops[i].output_metas[j]) == 0);
        }

        assert(loaded->ops[i].scalar_args[0] == ops[i].scalar_args[0]);

        assert(loaded->ops[i].input_trace_indices[0] == ops[i].input_trace_indices[0]);
        assert(loaded->ops[i].input_slot_ids[0] == ops[i].input_slot_ids[0]);
        assert(loaded->ops[i].input_slot_ids[1] == ops[i].input_slot_ids[1]);
        assert(loaded->ops[i].output_slot_ids[0] == ops[i].output_slot_ids[0]);
    }

    const crucible::ContentHash recomputed = crucible::compute_content_hash(std::span{loaded->ops, loaded->num_ops});
    assert(recomputed == original_content_hash);

    // Catches an uninitialised byte reaching the wire, which the
    // structural round-trip above cannot see.  Each call writes a
    // separately poisoned buffer, so neither can bias the other.
    {
        uint8_t buf_a[65536];
        uint8_t buf_b[65536];
        std::memset(buf_a, 0xAA, sizeof(buf_a));
        std::memset(buf_b, 0xBB, sizeof(buf_b));

        const size_t na = crucible::serialize_region(region, nullptr, std::span<uint8_t>{buf_a, sizeof(buf_a)});
        const size_t nb = crucible::serialize_region(region, nullptr, std::span<uint8_t>{buf_b, sizeof(buf_b)});
        assert(na == nb && "serialize_region byte count must be deterministic");
        assert(na > 0);
        assert(std::memcmp(buf_a, buf_b, na) == 0
               && "serialize_region bytes must be deterministic — uninit-memory leak?");
    }

    // Wire bytes are cross-process stable by construction: data_ptr is
    // zeroed on write, padding is explicit, and strong-typed fields go
    // out as raw bytes.  Any drift here is a wire-format change and
    // needs a CDAG_VERSION bump in the same commit as the golden.
    //
    // The byte length catches size drift, the hash catches content
    // drift at a stable size.
    {
        static constexpr uint32_t EXPECTED_CDAG_VERSION = 9;
        static_assert(crucible::CDAG_VERSION.value() == EXPECTED_CDAG_VERSION,
                      "CDAG_VERSION bump detected — update wire-byte golden below "
                      "after confirming the new bytes hash to the expected value.");

        const uint64_t wire_hash = fnv1a_bytes(std::span<const uint8_t>{buf, n});

        constexpr size_t EXPECTED_WIRE_BYTES = 1772;
        constexpr uint64_t EXPECTED_WIRE_HASH = 0x2943ef7ba87a2dc3ULL;

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

    uint8_t tiny_buf[4];
    const size_t n_tiny = crucible::serialize_region(region, nullptr, std::span<uint8_t>{tiny_buf, 4});
    assert(n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    crucible::Arena arena3(1 << 16);
    auto bad_region = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, 10}, arena3);
    crucible::RegionNode* bad = bad_region.value();
    assert(bad == nullptr && "deserialize_region must return nullptr on truncated input");

    // A header claiming 0xFFFFFFFF ops would otherwise drive an
    // allocation of four billion entries before anything failed.
    {
        // Offset 0..3 = magic; 4..7 = version; 8 = kind byte; 9..15 pad;
        // 16..23 merkle; 24..31 content; 32..35 num_ops.
        std::vector<uint8_t> adv(128, 0);
        const uint32_t magic = crucible::CDAG_MAGIC;
        const uint32_t version = crucible::CDAG_VERSION.value();
        const uint8_t kind = static_cast<uint8_t>(crucible::TraceNodeKind::REGION);
        std::memcpy(adv.data() + 0, &magic, 4);
        std::memcpy(adv.data() + 4, &version, 4);
        adv[8] = kind;
        // merkle_hash + content_hash stay zero.
        const uint32_t bogus_num_ops = 0xFFFFFFFFu;
        std::memcpy(adv.data() + 32, &bogus_num_ops, 4);

        crucible::Arena arena4(1 << 16);
        auto adversarial_region =
            crucible::deserialize_region(test.alloc, std::span<const uint8_t>{adv.data(), adv.size()}, arena4);
        crucible::RegionNode* r = adversarial_region.value();
        assert(r == nullptr && "deserialize_region must reject num_ops > CDAG_MAX_OPS");
    }

    // A loaded slot_id indexes a slot table sized to the plan's
    // num_slots.  The replay hot path checks only validity, so an
    // out-of-range slot_id would read past that table.  This region's
    // output slot ids are {0, 10, 20}, so num_slots 21 admits all of
    // them and num_slots 3 rejects two.  A plan-less region has no pool
    // to bound against and is exempt.
    {
        crucible::MemoryPlan ok_plan{};
        ok_plan.num_slots = 21;
        ok_plan.slots = arena.alloc_array<crucible::TensorSlot>(test.alloc, 21);
        std::uninitialized_value_construct_n(ok_plan.slots, 21);
        region->plan = &ok_plan;

        uint8_t pbuf[65536];
        const size_t pn = crucible::serialize_region(region, nullptr, std::span<uint8_t>{pbuf, sizeof(pbuf)});
        assert(pn > 0 && "plan-bearing serialize failed");
        crucible::Arena parena(1 << 16);
        auto ok_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{pbuf, pn}, parena);
        assert(ok_loaded.value() != nullptr && "plan-bearing region with in-range slot_ids must load");

        crucible::MemoryPlan bad_plan{};
        bad_plan.num_slots = 3;
        bad_plan.slots = arena.alloc_array<crucible::TensorSlot>(test.alloc, 3);
        std::uninitialized_value_construct_n(bad_plan.slots, 3);
        region->plan = &bad_plan;

        uint8_t nbuf[65536];
        const size_t nn = crucible::serialize_region(region, nullptr, std::span<uint8_t>{nbuf, sizeof(nbuf)});
        assert(nn > 0 && "plan-bearing (oob) serialize failed");
        crucible::Arena narena(1 << 16);
        auto bad_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{nbuf, nn}, narena);
        assert(bad_loaded.value() == nullptr && "deserialize_region must reject slot_id >= plan->num_slots");

        region->plan = nullptr;  // nothing later depends on this.
    }

    // An out-of-range pool_bytes becomes an [[assume]] under the release
    // contract semantic, so the load boundary must reject it against the
    // same constant the allocator checks.  num_slots is 21 so the
    // slot-id gate cannot fire, leaving pool_bytes the only rejector.
    // The bound is inclusive.
    {
        crucible::MemoryPlan over_plan{};
        over_plan.num_slots = 21;
        over_plan.slots = arena.alloc_array<crucible::TensorSlot>(test.alloc, 21);
        std::uninitialized_value_construct_n(over_plan.slots, 21);
        over_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes + 1;
        region->plan = &over_plan;

        uint8_t obuf[65536];
        const size_t on = crucible::serialize_region(region, nullptr, std::span<uint8_t>{obuf, sizeof(obuf)});
        assert(on > 0 && "over-pool_bytes serialize failed");
        crucible::Arena oarena(1 << 16);
        auto over_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{obuf, on}, oarena);
        assert(over_loaded.value() == nullptr && "deserialize_region must reject pool_bytes > kMaxPoolBytes");

        crucible::MemoryPlan at_plan{};
        at_plan.num_slots = 21;
        at_plan.slots = arena.alloc_array<crucible::TensorSlot>(test.alloc, 21);
        std::uninitialized_value_construct_n(at_plan.slots, 21);
        at_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes;
        region->plan = &at_plan;

        uint8_t abuf[65536];
        const size_t an = crucible::serialize_region(region, nullptr, std::span<uint8_t>{abuf, sizeof(abuf)});
        assert(an > 0 && "at-cap pool_bytes serialize failed");
        crucible::Arena aarena(1 << 16);
        auto at_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{abuf, an}, aarena);
        assert(at_loaded.value() != nullptr && "pool_bytes == kMaxPoolBytes must be accepted (inclusive)");

        region->plan = nullptr;  // restore.
    }

    std::printf("test_serialize: all tests passed\n");
    return 0;
}
