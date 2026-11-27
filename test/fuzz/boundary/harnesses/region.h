#pragma once

// A region image from the object store or from a peer.  The loader either
// refuses the bytes or returns a region that every consumer may trust:
// each tensor descriptor and each plan slot is in range, the plan fits the
// pool the allocator will build for it, the stored content hash is the hash
// of the loaded operations, and the region writes back to an image that
// loads to the same region.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <crucible/Serialize.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

inline void claim_region_trusted(HarnessName harness, const RegionNode& region) noexcept {
    for (const DagRef<MemoryPlan const>& plan_ref : region.plan) {
        const MemoryPlan& plan = *plan_ref;
        CRUCIBLE_FUZZ_CLAIM(harness, plan.pool_bytes <= PoolAllocator::kMaxPoolBytes);
        CRUCIBLE_FUZZ_CLAIM(harness, plan.num_external <= plan.num_slots());
        for (const TensorSlot& slot : plan.slots.view()) {
            CRUCIBLE_FUZZ_CLAIM(harness, ::fixy::enum_from<ScalarType>(static_cast<std::int8_t>(slot.dtype)).is_some());
            CRUCIBLE_FUZZ_CLAIM(harness,
                                ::fixy::enum_from<DeviceType>(static_cast<std::int8_t>(slot.device_type)).is_some());
            CRUCIBLE_FUZZ_CLAIM(harness, ::fixy::enum_from<Layout>(static_cast<std::int8_t>(slot.layout)).is_some());
            if (!slot.is_external) {
                const ::fixy::Option<std::uint64_t> end = ::fixy::checked_add(slot.offset_bytes, slot.nbytes);
                CRUCIBLE_FUZZ_CLAIM(harness, end.is_some());
                CRUCIBLE_FUZZ_CLAIM(harness,
                                    end.match([&plan](std::uint64_t last) noexcept { return last <= plan.pool_bytes; },
                                              [] noexcept { return false; }));
                CRUCIBLE_FUZZ_CLAIM(harness, slot.offset_bytes % PoolAllocator::ALIGNMENT == 0);
            }
        }
    }
    for (const TraceEntry& op : region.ops.view()) {
        // The loader gives each op each array of its count.
        CRUCIBLE_FUZZ_CLAIM(harness, op.input_metas().size() == op.num_inputs());
        CRUCIBLE_FUZZ_CLAIM(harness, op.output_metas().size() == op.num_outputs());
        // Each op passed the validator of an entry of the ring: a schema hash
        // other than zero, and the counts of the ring.
        CRUCIBLE_FUZZ_CLAIM(harness, op.schema_hash.raw() != 0);
        CRUCIBLE_FUZZ_CLAIM(harness, op.num_inputs() <= kMaxOpInputs);
        CRUCIBLE_FUZZ_CLAIM(harness, op.num_outputs() <= kMaxOpOutputs);
        CRUCIBLE_FUZZ_CLAIM(harness, op.num_scalar_args() <= kScalarSlots);
        for (const TensorMeta& meta : op.input_metas())
            claim_meta_in_range(harness, meta);
        for (const TensorMeta& meta : op.output_metas())
            claim_meta_in_range(harness, meta);
    }
    CRUCIBLE_FUZZ_CLAIM(harness, region.content_hash == compute_content_hash(region.ops.view()));
}

// The plan of a seed region: no plan, or a plan of two slots.
enum class SeedPlan : std::uint8_t {
    none,
    two_slots,
};

// A region the way the recorder builds one: two operations with one input
// and one output each, and with a plan of two slots for SeedPlan::two_slots.
// The fault of the second slot of a seed plan, which the region holds as
// const once it links the plan.
enum class SeedSlotFault : std::uint8_t {
    none,
    past_pool,
    misaligned
};

// The block size of each arena of the seeds and of each load.
inline constexpr ::fixy::Quantity<::fixy::unit::Byte, std::size_t> kRegionArenaBlock{std::size_t{1} << 20};

// The ops, their arrays, the region, the plan and its slots are in
// dag_arena, and one frame on it builds them.
[[nodiscard]] inline DagRef<RegionNode> build_seed_region(::fixy::BumpArena<DagArena>& dag_arena, SeedPlan seed_plan,
                                                          SeedSlotFault fault = SeedSlotFault::none) noexcept {
    return ::foundation::core::with_arena_frame(dag_arena, [seed_plan, fault](auto& frame) noexcept {
        const auto alloc = test_alloc();
        constexpr std::uint32_t kOps = 2;
        const bool has_plan = seed_plan == SeedPlan::two_slots;
        auto ops_run = make_op_run(alloc, frame, kOps);
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        for (std::uint32_t i = 0; i < kOps; ++i) {
            TraceEntry& op = *ops.get(i).expect("i is below kOps");
            op.schema_hash = SchemaHash{0xCAFE0000u + i};
            TensorMeta meta{};
            meta.set_shape(vector_tensor_shape(tensor_dim(16)));
            meta.dtype = ScalarType::Float;
            const auto input_metas_run = frame.array(alloc, 1, meta);
            const auto producers_run = frame.array(alloc, 1, OpIndex{});
            const auto input_slots_run = frame.array(alloc, 1, has_plan ? SlotId{0} : SlotId{});
            const auto holder = hold_op(ops_run, i);
            TraceEntry::set_inputs(frame, holder, input_metas_run, producers_run, input_slots_run);
            const auto output_metas_run = frame.array(alloc, 1, meta);
            const auto output_slots_run = frame.array(alloc, 1, SlotId{i});
            TraceEntry::set_outputs(frame, holder, output_metas_run, output_slots_run);
        }
        const auto region = make_region(alloc, frame, ops_run);
        if (has_plan) {
            const auto plan = frame.template make<MemoryPlan>(alloc);
            plan->pool_bytes = 2 * PoolAllocator::ALIGNMENT;
            plan->num_external = 0;
            plan->device_type = DeviceType::CPU;
            auto slots_run = frame.template array<TensorSlot>(alloc, 2, TensorSlot{});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<TensorSlot> slots = slots_run.view();
            std::uint32_t slot_index = 0;
            for (TensorSlot& slot : slots) {
                slot.offset_bytes = slot_index * PoolAllocator::ALIGNMENT;
                slot.nbytes = 64;
                slot.dtype = ScalarType::Float;
                slot.slot_id = SlotId{slot_index};
                ++slot_index;
            }
            TensorSlot& second = *slots.get(1).expect("the seed plan holds two slots");
            if (fault == SeedSlotFault::past_pool) second.nbytes = std::uint64_t{1} << 40;
            if (fault == SeedSlotFault::misaligned) second.offset_bytes += 8;
            frame.store(plan, &MemoryPlan::slots, slots_run);
            frame.store(region, &RegionNode::plan, plan);
        }
        return region.weaken();
    });
}

// The image of a region, as the writer writes it.
[[nodiscard]] inline SeedBytes region_image(const RegionNode& region) noexcept {
    ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(test_alloc(), std::size_t{1} << 16, std::byte{0});
    const auto [written, image] = ::fixy::with_mut(std::move(blank), [&region](auto run) noexcept {
        return serialize_region(::fixy::to(region), std::move(run));
    });
    return copy_seed(
        ::fixy::View<std::byte const>{image.view()}.window(0, written).expect("the writer writes inside its image"));
}

// Offsets of fields in a region image: the 32-byte header, then the op
// count, the first schema, the measured time and the variant, then the
// plan flag.
inline constexpr std::size_t kImageContentHashOffset = 24;
inline constexpr std::size_t kImageHasPlanOffset = 52;

// Images that an earlier loader accepted although they break a claim of the
// region type.  The loader must refuse each one.  They go after the seeds of
// seeds.
inline void add_region_regressions(SeedList& seeds) noexcept {
    ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);

    // The header states a content hash that is not the hash of the ops.
    SeedBytes wrong_hash = region_image(*build_seed_region(dag_arena, SeedPlan::none));
    wrong_hash.update(kImageContentHashOffset, [](std::byte& hash_byte) noexcept {
        hash_byte = static_cast<std::byte>(static_cast<unsigned>(hash_byte) ^ 0x01U);
    });
    add_seed(seeds, std::move(wrong_hash));

    // The plan flag holds a byte that is no value of a bool.
    SeedBytes bad_bool = region_image(*build_seed_region(dag_arena, SeedPlan::none));
    bad_bool.set(kImageHasPlanOffset, std::byte{0x02});
    add_seed(seeds, std::move(bad_bool));

    // A slot reaches past the pool, and a slot starts off the alignment.
    // The pool allocator aborts on either one.
    add_seed(seeds, region_image(*build_seed_region(dag_arena, SeedPlan::two_slots, SeedSlotFault::past_pool)));
    add_seed(seeds, region_image(*build_seed_region(dag_arena, SeedPlan::two_slots, SeedSlotFault::misaligned)));
}

[[nodiscard]] inline SeedList seeds_region() noexcept {
    ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);
    SeedList seeds = empty_seed_list();
    add_seed(seeds, region_image(*build_seed_region(dag_arena, SeedPlan::none)));
    add_seed(seeds, region_image(*build_seed_region(dag_arena, SeedPlan::two_slots)));
    add_region_regressions(seeds);
    return seeds;
}

inline void run_region(::fixy::View<std::byte const> bytes) noexcept {
    // Fuzz inputs stay small.  The cap keeps one input from spending the
    // whole run in one arena, and the loader's own ceiling is far above it.
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    const ::fixy::View<std::byte const> input =
        bytes.size() > kMaxInputBytes ? bytes.window(0, kMaxInputBytes).expect("the cap is inside the input") : bytes;

    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);
    const auto alloc = test_alloc();
    const ::fixy::Option<LoadedRegionNode> loaded = deserialize_region(alloc, input, arena).ok();
    for (const LoadedRegionNode& loaded_region : loaded) {
        const RegionNode& region = *loaded_region.value();
        claim_region_trusted(as_harness("region"), region);

        // The loaded region writes back to an image that loads to the same
        // region: the loader keeps every field the writer reads.
        ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(alloc, input.size() + 4096, std::byte{0});
        const auto [written, image] = ::fixy::with_mut(std::move(blank), [&region](auto run) noexcept {
            return serialize_region(::fixy::to(region), std::move(run));
        });
        CRUCIBLE_FUZZ_CLAIM("region", written > 0);
        const ::fixy::Option<LoadedRegionNode> reloaded =
            deserialize_region(alloc, image.view().window(0, written).expect("the writer writes inside its image"),
                               arena)
                .ok();
        CRUCIBLE_FUZZ_CLAIM("region", reloaded.is_some());
        for (const LoadedRegionNode& reloaded_region : reloaded) {
            const RegionNode& again = *reloaded_region.value();
            CRUCIBLE_FUZZ_CLAIM("region", again.num_ops() == region.num_ops());
            CRUCIBLE_FUZZ_CLAIM("region", again.content_hash == region.content_hash);
            CRUCIBLE_FUZZ_CLAIM("region", again.merkle_hash == region.merkle_hash);
        }
    }
}

}  // namespace crucible::fuzz::boundary
