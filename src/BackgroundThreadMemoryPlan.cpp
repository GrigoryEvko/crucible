// The body of the memory plan of crucible/BackgroundThread.h:
// compute_memory_plan, the sweep that gives each internal slot of a region
// its offset in one pool.  The publish stage of the pipeline calls it for
// each region that it publishes.
//
// The memory plan is a unit of its own, so its compile runs beside the
// compile of the pipeline unit src/BackgroundThread.cpp.

#include <crucible/BackgroundThread.h>

#include <crucible/TraceGraph.h>
#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

::fixy::Result<::fixy::Ref<MemoryPlan>, BackgroundThread::PlanRefusal>
BackgroundThread::compute_memory_plan(::foundation::effects::Alloc a, MemoryPlan& plan_storage,
                                      ::fixy::BumpArena& scratch, ::fixy::View<TensorSlot> slots) noexcept {
    static constexpr uint32_t ALIGNMENT = 256;

    MemoryPlan& plan = plan_storage;
    plan.slots = slots;
    const uint32_t num_slots = plan.num_slots();
    plan.num_external = 0;
    plan.pool_bytes = 0;

    plan.rank = rank_;
    plan.world_size = world_size_;
    plan.device_capability = device_capability_.value();
    plan.device_type = DeviceType::CPU;
    plan.device_idx = -1;
    plan.pad0 = decltype(plan.pad0){};

    if (num_slots == 0) return ::fixy::erase_brand(::fixy::to(plan));

    // The slot at an index of the sweep.  Each index of the sweep is a
    // position in the slots, so it is below their count.
    const auto slot_at = [slots](uint32_t index) noexcept -> TensorSlot& {
        return *slots.get(index).expect("an index of the sweep is below the count of the slots");
    };

    // The caller gives the slots, so the lifetime of each internal slot is
    // checked here, where the slots enter the sweep.  The sweep indexes its
    // arrays by birth_op and by death_op + 1, and it sizes them from the
    // largest death_op + 2.  A birth after the death indexes past the
    // arrays.  A death above MAX_DEATH_OP, also the none() value of
    // OpIndex, makes the sizes wrap.  An external slot keeps its own
    // allocation, and the sweep does not read its lifetime.
    //
    // The span of an internal slot is rounded up to the alignment.  A span
    // whose rounding does not fit in 64 bits has no slot of the pool, so the
    // plan refuses the region, and no rounding gives a slot that overlaps
    // another.  A span that saturated is such a span.
    static constexpr uint32_t MAX_DEATH_OP = UINT32_MAX - 3;
    uint32_t max_op = 0;
    for (const TensorSlot& slot : slots) {
        if (slot.is_external) {
            plan.num_external++;
        } else {
            CRUCIBLE_FATAL_INVARIANT(slot.birth_op.raw() <= slot.death_op.raw());
            CRUCIBLE_FATAL_INVARIANT(slot.death_op.raw() <= MAX_DEATH_OP);
            if (::fixy::align_up(slot.nbytes, uint64_t{ALIGNMENT}).is_none()) [[unlikely]] {
                return ::fixy::err(PlanRefusal::SpanTooLarge);
            }
            if (plan.device_type == DeviceType::CPU && slot.device_type != DeviceType::CPU) {
                plan.device_type = slot.device_type;
                plan.device_idx = slot.device_idx;
            }
            if (slot.death_op.raw() + 1 > max_op) max_op = slot.death_op.raw() + 1;
        }
    }

    uint32_t num_internal = num_slots - plan.num_external;
    if (num_internal == 0) return ::fixy::erase_brand(::fixy::to(plan));

    // Bucket the slots by birth_op and by death_op + 1.  The sweep range
    // is [0, max_op].  The checks above keep max_op in [1, UINT32_MAX - 2],
    // so num_ops and num_ops + 1 are positive and do not wrap.
    uint32_t num_ops = max_op + 1;
    const ::fixy::View<uint32_t> birth_count = scratch.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> death_count = scratch.array<uint32_t>(a, num_ops, 0);

    for (const TensorSlot& slot : slots) {
        if (slot.is_external) continue;
        ++detail::csr_element_(birth_count, slot.birth_op.raw());
        uint32_t free_at = slot.death_op.raw() + 1;
        if (free_at < num_ops) ++detail::csr_element_(death_count, free_at);
    }

    // The first offset of each array is the zero of the fill.
    const ::fixy::View<uint32_t> birth_off = scratch.array<uint32_t>(a, num_ops + 1, 0);
    const ::fixy::View<uint32_t> death_off = scratch.array<uint32_t>(a, num_ops + 1, 0);
    for (uint32_t o = 0; o < num_ops; o++) {
        detail::csr_element_(birth_off, o + 1) =
            detail::csr_element_(birth_off, o) + detail::csr_element_(birth_count, o);
        detail::csr_element_(death_off, o + 1) =
            detail::csr_element_(death_off, o) + detail::csr_element_(death_count, o);
    }

    const ::fixy::View<uint32_t> born_slots = scratch.array<uint32_t>(a, num_internal, 0);
    const ::fixy::View<uint32_t> dead_slots = scratch.array<uint32_t>(a, num_internal, 0);
    // Cursors start as copies of the offsets and advance during scatter.
    const ::fixy::View<uint32_t> birth_cur = scratch.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> death_cur = scratch.array<uint32_t>(a, num_ops, 0);
    ::fixy::copy(birth_cur, birth_off.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");
    ::fixy::copy(death_cur, death_off.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");

    for (uint32_t s = 0; s < num_slots; s++) {
        const TensorSlot& slot = slot_at(s);
        if (slot.is_external) continue;
        detail::csr_element_(born_slots, detail::csr_element_(birth_cur, slot.birth_op.raw())++) = s;
        uint32_t free_at = slot.death_op.raw() + 1;
        if (free_at < num_ops) detail::csr_element_(dead_slots, detail::csr_element_(death_cur, free_at)++) = s;
    }

    // The free list keeps sizes and offsets in separate arrays so the
    // first-fit scan touches only sizes.  Freeing appends, allocation
    // scans then swap-removes.  A sorted list with coalescing is the
    // alternative and it loses: it costs an insertion and a memmove per
    // operation, while the direct reuse matching below already captures
    // most same-size matches and keeps the list short.
    //
    // free_list_count bounds every read.  A free list of MAX_FREE blocks
    // drops each further block.
    constexpr uint32_t MAX_FREE = 4096;
    using FreeSlot = ::fixy::Index<MAX_FREE>;
    ::fixy::FixedArray<uint64_t, MAX_FREE> free_list_sizes{};
    ::fixy::FixedArray<uint64_t, MAX_FREE> free_list_offsets{};
    uint32_t free_list_count = 0;
    uint64_t pool_end = 0;
    // Set when a block at the end of the pool would pass 2^64 - 1.  The
    // sweep then stops, and the plan refuses the region.
    bool is_pool_end_past_range = false;

    auto free_block = [&](uint64_t offset, uint64_t size) {
        for (const FreeSlot next : FreeSlot::check(free_list_count)) {
            free_list_offsets.with_element(next, [offset](uint64_t& held) noexcept { held = offset; });
            free_list_sizes.with_element(next, [size](uint64_t& held) noexcept { held = size; });
            ++free_list_count;
        }
    };

    // The first fit of the scan of the sizes, then a swap with the last
    // block or a cut of the fit.  Complexity: O(free_list_count).
    auto alloc_slot = [&](uint32_t s, uint64_t aligned_size) {
        const ::fixy::Option<std::size_t> fit = ::fixy::find_if(
            free_list_sizes.view().window(0, free_list_count).expect("the free list holds at most MAX_FREE blocks"),
            [aligned_size](uint64_t size) noexcept { return size >= aligned_size; });
        for (const std::size_t position : fit) {
            const FreeSlot block = FreeSlot::check(position).expect("a position of the free list is below MAX_FREE");
            slot_at(s).offset_bytes = free_list_offsets.at(block);
            if (free_list_sizes.at(block) == aligned_size) {
                --free_list_count;
                const FreeSlot last = FreeSlot::check(free_list_count).expect("the last block is below MAX_FREE");
                const uint64_t last_size = free_list_sizes.at(last);
                const uint64_t last_offset = free_list_offsets.at(last);
                free_list_sizes.with_element(block, [last_size](uint64_t& held) noexcept { held = last_size; });
                free_list_offsets.with_element(block, [last_offset](uint64_t& held) noexcept { held = last_offset; });
            } else {
                free_list_offsets.with_element(block,
                                               [aligned_size](uint64_t& held) noexcept { held += aligned_size; });
                free_list_sizes.with_element(block, [aligned_size](uint64_t& held) noexcept { held -= aligned_size; });
            }
            return;
        }
        ::fixy::Option<uint64_t> end = ::fixy::checked_add(pool_end, aligned_size);
        if (end.is_none()) [[unlikely]] {
            is_pool_end_past_range = true;
            return;
        }
        slot_at(s).offset_bytes = pool_end;
        pool_end = std::move(end).expect("the test above found the end");
    };

    struct DyingInfo {
        uint64_t aligned_size = 0;
        uint64_t offset = 0;
    };
    constexpr uint32_t MAX_PER_OP = 64;
    using PerOpSlot = ::fixy::Index<MAX_PER_OP>;
    // The slot of a position below a capped count.  The cap keeps it below
    // MAX_PER_OP, so the optimizer removes the check.
    auto prove_per_op = [](uint32_t position) noexcept {
        return PerOpSlot::check(position).expect("a capped count is at most MAX_PER_OP");
    };
    // The bytes of a slot rounded up to the alignment of the pool.  The first
    // pass refused each internal slot whose rounding does not fit.
    auto aligned_bytes_of = [](uint64_t nbytes) noexcept {
        return ::fixy::align_up(nbytes, uint64_t{ALIGNMENT})
            .expect("the first pass refused each span that does not fit");
    };

    for (uint32_t op = 0; op < num_ops; op++) {
        uint32_t dying_begin = detail::csr_element_(death_off, op);
        uint32_t dying_end = detail::csr_element_(death_off, op + 1);
        uint32_t born_begin = detail::csr_element_(birth_off, op);
        uint32_t born_end = detail::csr_element_(birth_off, op + 1);
        uint32_t num_dying = dying_end - dying_begin;
        uint32_t num_born = born_end - born_begin;

        // Match a dying slot to a born slot directly, bypassing the
        // free list entirely.
        uint32_t dying_count_capped = num_dying < MAX_PER_OP ? num_dying : MAX_PER_OP;
        uint32_t born_count_capped = num_born < MAX_PER_OP ? num_born : MAX_PER_OP;
        ::fixy::FixedArray<bool, MAX_PER_OP> dying_consumed{};
        ::fixy::FixedArray<bool, MAX_PER_OP> born_assigned{};

        if (dying_count_capped > 0 && born_count_capped > 0) {
            ::fixy::FixedArray<DyingInfo, MAX_PER_OP> dying_slot_info{};
            for (uint32_t i = 0; i < dying_count_capped; i++) {
                const TensorSlot& dying_slot = slot_at(detail::csr_element_(dead_slots, dying_begin + i));
                const DyingInfo dying = {.aligned_size = aligned_bytes_of(dying_slot.nbytes),
                                         .offset = dying_slot.offset_bytes};
                dying_slot_info.with_element(prove_per_op(i), [dying](DyingInfo& held) noexcept { held = dying; });
            }

            for (uint32_t born_idx = 0; born_idx < born_count_capped; born_idx++) {
                TensorSlot& born_slot = slot_at(detail::csr_element_(born_slots, born_begin + born_idx));
                uint64_t born_aligned_size = aligned_bytes_of(born_slot.nbytes);

                uint32_t best_dying_match_idx = UINT32_MAX;
                uint64_t best_waste_bytes = UINT64_MAX;
                for (uint32_t dying_idx = 0; dying_idx < dying_count_capped; dying_idx++) {
                    const DyingInfo& dying_info = dying_slot_info.at(prove_per_op(dying_idx));
                    if (!dying_consumed.at(prove_per_op(dying_idx)) && dying_info.aligned_size >= born_aligned_size) {
                        uint64_t waste_bytes = dying_info.aligned_size - born_aligned_size;
                        if (waste_bytes < best_waste_bytes) {
                            best_dying_match_idx = dying_idx;
                            best_waste_bytes = waste_bytes;
                        }
                    }
                }
                if (best_dying_match_idx != UINT32_MAX) {
                    const PerOpSlot best_match = prove_per_op(best_dying_match_idx);
                    born_slot.offset_bytes = dying_slot_info.at(best_match).offset;
                    dying_consumed.with_element(best_match, [](bool& is_consumed) noexcept { is_consumed = true; });
                    born_assigned.with_element(prove_per_op(born_idx),
                                               [](bool& is_assigned) noexcept { is_assigned = true; });
                    if (best_waste_bytes > 0)
                        free_block(dying_slot_info.at(best_match).offset + born_aligned_size, best_waste_bytes);
                }
            }
        }

        for (uint32_t i = 0; i < num_dying; i++) {
            if (i < dying_count_capped && dying_consumed.at(prove_per_op(i))) continue;
            const TensorSlot& dying_slot = slot_at(detail::csr_element_(dead_slots, dying_begin + i));
            free_block(dying_slot.offset_bytes, aligned_bytes_of(dying_slot.nbytes));
        }

        for (uint32_t i = 0; i < num_born; i++) {
            if (i < born_count_capped && born_assigned.at(prove_per_op(i))) continue;
            uint32_t born_slot_id = detail::csr_element_(born_slots, born_begin + i);
            alloc_slot(born_slot_id, aligned_bytes_of(slot_at(born_slot_id).nbytes));
        }
        if (is_pool_end_past_range) [[unlikely]] {
            return ::fixy::err(PlanRefusal::PoolEndOverflow);
        }
    }

    plan.pool_bytes = pool_end;
    return ::fixy::erase_brand(::fixy::to(plan));
}

}  // namespace crucible
