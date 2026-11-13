#pragma once

// A region image from the object store or from a peer.  The loader either
// refuses the bytes or returns a region that every consumer may trust:
// each tensor descriptor and each plan slot is in range, the plan fits the
// pool the allocator will build for it, the stored content hash is the hash
// of the loaded operations, and the region writes back to an image that
// loads to the same region.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <crucible/Serialize.h>

#include <memory>
#include <vector>

namespace crucible::fuzz::boundary {

inline void claim_region_trusted(const char* harness, const RegionNode& region) {
    if (region.plan != nullptr) {
        const MemoryPlan& plan = *region.plan;
        CRUCIBLE_FUZZ_CLAIM(harness, plan.pool_bytes <= PoolAllocator::kMaxPoolBytes);
        CRUCIBLE_FUZZ_CLAIM(harness, plan.num_external <= plan.num_slots());
        for (const TensorSlot& slot : plan.slots) {
            CRUCIBLE_FUZZ_CLAIM(harness, valid_scalar_type(static_cast<std::int8_t>(slot.dtype)));
            CRUCIBLE_FUZZ_CLAIM(harness, valid_device_type(static_cast<std::int8_t>(slot.device_type)));
            CRUCIBLE_FUZZ_CLAIM(harness, valid_layout(static_cast<std::int8_t>(slot.layout)));
            if (!slot.is_external) {
                std::uint64_t end = 0;
                CRUCIBLE_FUZZ_CLAIM(harness, !__builtin_add_overflow(slot.offset_bytes, slot.nbytes, &end));
                CRUCIBLE_FUZZ_CLAIM(harness, end <= plan.pool_bytes);
                CRUCIBLE_FUZZ_CLAIM(harness, slot.offset_bytes % PoolAllocator::ALIGNMENT == 0);
            }
        }
    }
    for (const TraceEntry& op : region.ops) {
        // The loader gives each op each array of its count.
        CRUCIBLE_FUZZ_CLAIM(harness, op.input_metas().size() == op.num_inputs());
        CRUCIBLE_FUZZ_CLAIM(harness, op.output_metas().size() == op.num_outputs());
        for (const TensorMeta& meta : op.input_metas())
            claim_meta_in_range(harness, meta);
        for (const TensorMeta& meta : op.output_metas())
            claim_meta_in_range(harness, meta);
    }
    CRUCIBLE_FUZZ_CLAIM(harness, region.content_hash == compute_content_hash(region.ops));
}

// A region the way the recorder builds one: two operations with one input
// and one output each, and with a plan of two slots when with_plan is set.
// The ops, their arrays, the region and the slots of the plan are in
// dag_arena.
[[nodiscard]] inline RegionNode* build_seed_region(Arena& arena, ::fixy::BumpArena& dag_arena, bool with_plan) {
    const auto alloc = test_alloc();
    constexpr std::uint32_t kOps = 2;
    const ::fixy::View<TraceEntry> ops = dag_arena.array<TraceEntry>(alloc, kOps, TraceEntry{});
    for (std::uint32_t i = 0; i < kOps; ++i) {
        TraceEntry& op = *ops.get(i).expect("i is below kOps");
        op.schema_hash = SchemaHash{0xCAFE0000u + i};
        op.set_num_inputs(1);
        op.set_num_outputs(1);
        TensorMeta meta{};
        meta.ndim = 1;
        meta.sizes[0] = tensor_dim(16);
        meta.strides[0] = tensor_dim(1);
        meta.dtype = ScalarType::Float;
        op.set_input_metas(dag_arena.array(alloc, 1, meta));
        op.set_output_metas(dag_arena.array(alloc, 1, meta));
        op.set_input_trace_indices(dag_arena.array(alloc, 1, OpIndex{}));
        op.set_input_slot_ids(dag_arena.array(alloc, 1, with_plan ? SlotId{0} : SlotId{}));
        op.set_output_slot_ids(dag_arena.array(alloc, 1, SlotId{i}));
    }
    RegionNode* region = make_region(alloc, dag_arena, ops);
    if (with_plan) {
        auto* plan = arena.alloc_obj<MemoryPlan>(alloc);
        plan->pool_bytes = 2 * PoolAllocator::ALIGNMENT;
        plan->num_external = 0;
        plan->device_type = DeviceType::CPU;
        plan->slots = dag_arena.array<TensorSlot>(alloc, 2, TensorSlot{});
        std::uint32_t slot_index = 0;
        for (TensorSlot& slot : plan->slots) {
            slot.offset_bytes = slot_index * PoolAllocator::ALIGNMENT;
            slot.nbytes = 64;
            slot.dtype = ScalarType::Float;
            slot.slot_id = SlotId{slot_index};
            ++slot_index;
        }
        region->plan = plan;
    }
    return region;
}

[[nodiscard]] inline std::vector<std::uint8_t> region_image(const RegionNode* region) {
    std::vector<std::uint8_t> image(std::size_t{1} << 16);
    image.resize(serialize_region(SerializedRegion{*region}, SerialBuffer{image}));
    return image;
}

// Offsets of fields in a region image: the 32-byte header, then the op
// count, the first schema, the measured time and the variant, then the
// plan flag.
inline constexpr std::size_t kImageContentHashOffset = 24;
inline constexpr std::size_t kImageHasPlanOffset = 52;

// Images that an earlier loader accepted although they break a claim of the
// region type.  The loader must refuse each one.
[[nodiscard]] inline Seeds region_regressions() {
    Arena arena;
    ::fixy::BumpArena dag_arena{};
    Seeds seeds;

    // The header states a content hash that is not the hash of the ops.
    auto wrong_hash = region_image(build_seed_region(arena, dag_arena, false));
    wrong_hash[kImageContentHashOffset] ^= 0x01;
    seeds.push_back(std::move(wrong_hash));

    // The plan flag holds a byte that is no value of a bool.
    auto bad_bool = region_image(build_seed_region(arena, dag_arena, false));
    bad_bool[kImageHasPlanOffset] = 0x02;
    seeds.push_back(std::move(bad_bool));

    // A slot reaches past the pool, and a slot starts off the alignment.
    // The pool allocator aborts on either one.
    RegionNode* past_pool = build_seed_region(arena, dag_arena, true);
    past_pool->plan->slots.get(1).expect("the seed plan holds two slots")->nbytes = std::uint64_t{1} << 40;
    seeds.push_back(region_image(past_pool));
    RegionNode* misaligned = build_seed_region(arena, dag_arena, true);
    misaligned->plan->slots.get(1).expect("the seed plan holds two slots")->offset_bytes += 8;
    seeds.push_back(region_image(misaligned));
    return seeds;
}

[[nodiscard]] inline Seeds seeds_region() {
    Arena arena;
    ::fixy::BumpArena dag_arena{};
    Seeds seeds{region_image(build_seed_region(arena, dag_arena, false)),
                region_image(build_seed_region(arena, dag_arena, true))};
    for (auto& regression : region_regressions())
        seeds.push_back(std::move(regression));
    return seeds;
}

inline void run_region(std::span<const std::uint8_t> bytes) {
    // Fuzz inputs stay small.  The cap keeps one input from spending the
    // whole run in one arena, and the loader's own ceiling is far above it.
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    if (bytes.size() > kMaxInputBytes) bytes = bytes.first(kMaxInputBytes);

    ::fixy::BumpArena arena{};
    const auto alloc = test_alloc();
    const auto loaded = deserialize_region(alloc, bytes, arena);
    if (!loaded) return;
    RegionNode* region = loaded->value();
    claim_region_trusted("region", *region);

    // The loaded region writes back to an image that loads to the same
    // region: the loader keeps every field the writer reads.
    std::vector<std::uint8_t> image(bytes.size() + 4096);
    const std::size_t written = serialize_region(SerializedRegion{*region}, SerialBuffer{image});
    CRUCIBLE_FUZZ_CLAIM("region", written > 0);
    const auto reloaded = deserialize_region(alloc, std::span<const std::uint8_t>{image.data(), written}, arena);
    CRUCIBLE_FUZZ_CLAIM("region", reloaded.has_value());
    RegionNode* again = reloaded->value();
    CRUCIBLE_FUZZ_CLAIM("region", again->num_ops() == region->num_ops());
    CRUCIBLE_FUZZ_CLAIM("region", again->content_hash == region->content_hash);
    CRUCIBLE_FUZZ_CLAIM("region", again->merkle_hash == region->merkle_hash);
}

}  // namespace crucible::fuzz::boundary
