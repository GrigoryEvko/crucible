#include <crucible/Arena.h>
#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>
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
    auto test = ::foundation::effects::testing::test();
    crucible::Arena arena(1 << 16);
    ::fixy::BumpArena dag_arena{1 << 16};

    constexpr uint32_t NUM_OPS = 3;
    // A storage of more than 4 GiB, which a size of 32 bits cannot hold.
    constexpr uint64_t kLargeStorageBytes = 98000000000ULL;
    const ::fixy::View<crucible::TraceEntry> ops =
        dag_arena.array<crucible::TraceEntry>(test.alloc, NUM_OPS, crucible::TraceEntry{});

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        crucible::TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
        op.schema_hash = crucible::SchemaHash{0xAA + i * 0x11};
        op.shape_hash = crucible::ShapeHash{0x100 + i};
        op.scope_hash = crucible::ScopeHash{0x200 + i};
        op.callsite_hash = crucible::CallsiteHash{0x300 + i};
        op.num_inputs = 2;
        op.num_outputs = 1;
        op.num_scalar_args = 1;
        op.grad_enabled = (i % 2 == 0);
        // Vary op_flags across ops to exercise bit-packing round-trip.
        op.inference_mode = (i == 0);
        op.is_mutable = (i == 1);
        op.training_phase = static_cast<crucible::TrainingPhase>(i % 4);
        op.torch_function = (i == 2);

        op.input_metas = arena.alloc_array<crucible::TensorMeta>(test.alloc, 2);
        op.input_metas[0] = make_meta(4, 8);
        op.input_metas[1] = make_meta(4, 8);
        op.input_metas[0].storage_nbytes = kLargeStorageBytes;

        op.output_metas = arena.alloc_array<crucible::TensorMeta>(test.alloc, 1);
        op.output_metas[0] = make_meta(4, 8);

        op.scalar_args = arena.alloc_array<int64_t>(test.alloc, 1);
        op.scalar_args[0] = static_cast<int64_t>(i * 42);

        op.input_trace_indices = arena.alloc_array<crucible::OpIndex>(test.alloc, 2);
        op.input_trace_indices[0] = (i > 0) ? crucible::OpIndex{i - 1} : crucible::OpIndex{};
        op.input_trace_indices[1] = crucible::OpIndex{};

        op.input_slot_ids = arena.alloc_array<crucible::SlotId>(test.alloc, 2);
        op.input_slot_ids[0] = (i > 0) ? crucible::SlotId{(i - 1) * 10} : crucible::SlotId{};
        op.input_slot_ids[1] = crucible::SlotId{};

        op.output_slot_ids = arena.alloc_array<crucible::SlotId>(test.alloc, 1);
        op.output_slot_ids[0] = crucible::SlotId{i * 10};
    }

    auto* region = crucible::make_region(test.alloc, dag_arena, ops);
    assert(region != nullptr);
    assert(region->num_ops() == NUM_OPS);

    const crucible::ContentHash original_content_hash = region->content_hash;
    const crucible::MerkleHash original_merkle_hash = region->merkle_hash;
    assert(static_cast<bool>(original_content_hash));

    uint8_t buf[65536];
    const size_t n = crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{buf});
    assert(n > 0 && "serialize_region returned 0 — buffer too small or bad region");

    ::fixy::BumpArena arena2{1 << 16};
    auto loaded_region = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, n}, arena2);
    assert(loaded_region.has_value() && "deserialize_region returned no region");
    crucible::RegionNode* loaded = loaded_region->value();

    assert(loaded->content_hash == original_content_hash);
    assert(loaded->merkle_hash == original_merkle_hash);
    assert(loaded->num_ops() == NUM_OPS);
    assert(loaded->kind == crucible::TraceNodeKind::REGION);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        const crucible::TraceEntry& loaded_op = loaded->op_at(i);
        const crucible::TraceEntry& source_op = *ops.get(i).expect("i is below NUM_OPS");
        assert(loaded_op.schema_hash == source_op.schema_hash);
        assert(loaded_op.shape_hash == source_op.shape_hash);
        assert(loaded_op.scope_hash == source_op.scope_hash);
        assert(loaded_op.num_inputs == source_op.num_inputs);
        assert(loaded_op.num_outputs == source_op.num_outputs);
        assert(loaded_op.num_scalar_args == source_op.num_scalar_args);

        // These five fields share one packed byte on the wire.
        assert(loaded_op.grad_enabled == source_op.grad_enabled);
        assert(loaded_op.inference_mode == source_op.inference_mode);
        assert(loaded_op.is_mutable == source_op.is_mutable);
        assert(loaded_op.training_phase == source_op.training_phase);
        assert(loaded_op.torch_function == source_op.torch_function);

        for (uint16_t j = 0; j < source_op.num_inputs; j++) {
            assert(crucible::raw_data_ptr(loaded_op.input_metas[j]) == nullptr
                   && "data_ptr must be null after deserialization");
            assert(crucible::raw_grad_fn_hash(source_op.input_metas[j]) != 0
                   && "test fixture must exercise non-zero Family-B input");
            assert(crucible::raw_grad_fn_hash(loaded_op.input_metas[j]) == 0
                   && "grad_fn_hash must be zero after deserialization");
            assert(loaded_op.input_metas[j].ndim == source_op.input_metas[j].ndim);
            assert(loaded_op.input_metas[j].dtype == source_op.input_metas[j].dtype);
            assert(crucible::raw_tensor_dim(loaded_op.input_metas[j].sizes[0])
                   == crucible::raw_tensor_dim(source_op.input_metas[j].sizes[0]));
            assert(crucible::raw_tensor_dim(loaded_op.input_metas[j].sizes[1])
                   == crucible::raw_tensor_dim(source_op.input_metas[j].sizes[1]));
            assert(crucible::raw_tensor_dim(loaded_op.input_metas[j].strides[0])
                   == crucible::raw_tensor_dim(source_op.input_metas[j].strides[0]));
            assert(crucible::raw_tensor_dim(loaded_op.input_metas[j].strides[1])
                   == crucible::raw_tensor_dim(source_op.input_metas[j].strides[1]));
        }
        for (uint16_t j = 0; j < source_op.num_outputs; j++) {
            assert(crucible::raw_data_ptr(loaded_op.output_metas[j]) == nullptr);
            assert(crucible::raw_grad_fn_hash(source_op.output_metas[j]) != 0);
            assert(crucible::raw_grad_fn_hash(loaded_op.output_metas[j]) == 0);
        }

        assert(loaded_op.input_metas[0].storage_nbytes == kLargeStorageBytes
               && "a storage size above 4 GiB must keep each of its 64 bits");

        assert(loaded_op.scalar_args[0] == source_op.scalar_args[0]);

        assert(loaded_op.input_trace_indices[0] == source_op.input_trace_indices[0]);
        assert(loaded_op.input_slot_ids[0] == source_op.input_slot_ids[0]);
        assert(loaded_op.input_slot_ids[1] == source_op.input_slot_ids[1]);
        assert(loaded_op.output_slot_ids[0] == source_op.output_slot_ids[0]);
    }

    const crucible::ContentHash recomputed = crucible::compute_content_hash(loaded->ops);
    assert(recomputed == original_content_hash);

    // Catches an uninitialised byte reaching the wire, which the
    // structural round-trip above cannot see.  Each call writes a
    // separately poisoned buffer, so neither can bias the other.
    {
        uint8_t buf_a[65536];
        uint8_t buf_b[65536];
        std::memset(buf_a, 0xAA, sizeof(buf_a));
        std::memset(buf_b, 0xBB, sizeof(buf_b));

        const size_t na =
            crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{buf_a});
        const size_t nb =
            crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{buf_b});
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
        static constexpr uint32_t EXPECTED_CDAG_VERSION = 11;
        static_assert(crucible::CDAG_VERSION.value() == EXPECTED_CDAG_VERSION,
                      "CDAG_VERSION bump detected — update wire-byte golden below "
                      "after confirming the new bytes hash to the expected value.");

        const uint64_t wire_hash = fnv1a_bytes(std::span<const uint8_t>{buf, n});

        // CDAG_VERSION 11 writes the storage size of each meta in 8 bytes,
        // not 4.  The image holds 9 metas, so it is 36 bytes longer.
        constexpr size_t EXPECTED_WIRE_BYTES = 1808;
        constexpr uint64_t EXPECTED_WIRE_HASH = 0x848a85ba074a6246ULL;

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
    const size_t n_tiny =
        crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{tiny_buf});
    assert(n_tiny == 0 && "serialize_region must return 0 on buffer overflow");

    ::fixy::BumpArena arena3{1 << 16};
    auto bad_region = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{buf, 10}, arena3);
    assert(!bad_region.has_value() && "deserialize_region must refuse a truncated input");

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

        ::fixy::BumpArena arena4{1 << 16};
        auto adversarial_region =
            crucible::deserialize_region(test.alloc, std::span<const uint8_t>{adv.data(), adv.size()}, arena4);
        assert(!adversarial_region.has_value() && "deserialize_region must reject num_ops > CDAG_MAX_OPS");
    }

    // A loaded slot_id indexes a slot table sized to the plan's
    // num_slots.  The replay hot path checks only validity, so an
    // out-of-range slot_id would read past that table.  This region's
    // output slot ids are {0, 10, 20}, so num_slots 21 admits all of
    // them and num_slots 3 rejects two.  A plan-less region has no pool
    // to bound against and is exempt.
    {
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan ok_plan{};
        ok_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        region->plan = &ok_plan;

        uint8_t pbuf[65536];
        const size_t pn = crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{pbuf});
        assert(pn > 0 && "plan-bearing serialize failed");
        ::fixy::BumpArena parena{1 << 16};
        auto ok_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{pbuf, pn}, parena);
        assert(ok_loaded.has_value() && "plan-bearing region with in-range slot_ids must load");

        crucible::MemoryPlan bad_plan{};
        bad_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 3, crucible::TensorSlot{});
        region->plan = &bad_plan;

        uint8_t nbuf[65536];
        const size_t nn = crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{nbuf});
        assert(nn > 0 && "plan-bearing (oob) serialize failed");
        ::fixy::BumpArena narena{1 << 16};
        auto bad_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{nbuf, nn}, narena);
        assert(!bad_loaded.has_value() && "deserialize_region must reject slot_id >= plan->num_slots");

        region->plan = nullptr;  // nothing later depends on this.
    }

    // An out-of-range pool_bytes becomes an [[assume]] under the release
    // contract semantic, so the load boundary must reject it against the
    // same constant the allocator checks.  num_slots is 21 so the
    // slot-id gate cannot fire, leaving pool_bytes the only rejector.
    // The bound is inclusive.
    {
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan over_plan{};
        over_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        over_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes + 1;
        region->plan = &over_plan;

        uint8_t obuf[65536];
        const size_t on = crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{obuf});
        assert(on > 0 && "over-pool_bytes serialize failed");
        ::fixy::BumpArena oarena{1 << 16};
        auto over_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{obuf, on}, oarena);
        assert(!over_loaded.has_value() && "deserialize_region must reject pool_bytes > kMaxPoolBytes");

        crucible::MemoryPlan at_plan{};
        at_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        at_plan.pool_bytes = crucible::PoolAllocator::kMaxPoolBytes;
        region->plan = &at_plan;

        uint8_t abuf[65536];
        const size_t an = crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{abuf});
        assert(an > 0 && "at-cap pool_bytes serialize failed");
        ::fixy::BumpArena aarena{1 << 16};
        auto at_loaded = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{abuf, an}, aarena);
        assert(at_loaded.has_value() && "pool_bytes == kMaxPoolBytes must be accepted (inclusive)");

        region->plan = nullptr;  // restore.
    }

    // A writer sets every pad byte of a slot to zero, so one non-zero byte
    // in a pad refuses the whole image.  The last slot carries a slot id
    // that occurs once in the image, which locates its pads: the 3-byte
    // pad ends at the slot id, and the 4-byte pad2 starts after it.
    {
        constexpr uint32_t kMarkerSlotId = 0x5A5A5A5Au;
        ::fixy::BumpArena slot_arena{1 << 16};
        crucible::MemoryPlan pad_plan{};
        pad_plan.slots = slot_arena.array<crucible::TensorSlot>(test.alloc, 21, crucible::TensorSlot{});
        pad_plan.slots.get(20).expect("the pad plan holds 21 slots")->slot_id = crucible::SlotId{kMarkerSlotId};
        region->plan = &pad_plan;

        std::vector<uint8_t> image(65536);
        const size_t image_bytes =
            crucible::serialize_region(crucible::SerializedRegion{*region}, crucible::SerialBuffer{image});
        assert(image_bytes > 0 && "pad-probe serialize failed");
        image.resize(image_bytes);

        uint8_t marker[sizeof(kMarkerSlotId)];
        std::memcpy(marker, &kMarkerSlotId, sizeof(marker));
        size_t marker_at = image_bytes;
        size_t marker_count = 0;
        for (size_t at = 0; at + sizeof(marker) <= image_bytes; ++at) {
            if (std::memcmp(image.data() + at, marker, sizeof(marker)) == 0) {
                marker_at = at;
                ++marker_count;
            }
        }
        assert(marker_count == 1 && "the marker slot id must occur once in the image");
        const size_t pad_at = marker_at - 3;
        const size_t pad2_at = marker_at + sizeof(marker);

        ::fixy::BumpArena clean_arena{1 << 16};
        auto clean = crucible::deserialize_region(test.alloc, std::span<const uint8_t>{image}, clean_arena);
        assert(clean.has_value() && "the unmodified pad-probe image must load");

        const auto refuses_with_byte_set_at = [&](size_t at) {
            std::vector<uint8_t> corrupt = image;
            corrupt[at] = 1;
            ::fixy::BumpArena corrupt_arena{1 << 16};
            return !crucible::deserialize_region(test.alloc, std::span<const uint8_t>{corrupt}, corrupt_arena)
                        .has_value();
        };
        for (size_t byte = 0; byte < 4; ++byte) {
            assert(refuses_with_byte_set_at(pad2_at + byte) && "a non-zero byte in pad2 must refuse the image");
        }
        for (size_t byte = 0; byte < 3; ++byte) {
            assert(refuses_with_byte_set_at(pad_at + byte) && "a non-zero byte in the slot pad must refuse the image");
        }

        region->plan = nullptr;
    }

    crucible::test::pass("test_serialize: all tests passed\n");
    return 0;
}
