#pragma once

// The memory plan of a test, in the arena of a frame.
//
// MemoryPlan::slots is a run slot of a frame, so only a store of the frame of
// the plan fills it, with a run of the same frame.  A test that fills its
// slots in a run of its own gives them to these helpers.  The helpers copy the
// slots into a run of the frame, let the planner give each internal slot its
// offset there, and store the run in the plan.  A test reads the planned slots
// through plan->slots.

#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>

namespace crucible::test {

// The plan of a copy of slots, in the arena of the frame, or the refusal of
// the planner.  The plan holds its slots only when the planner gives it.
// Complexity: O(n) for n slots, plus the sweep of the planner.
template <class Marker, class Fresh>
[[nodiscard]] ::fixy::Result<DagFrameRef<MemoryPlan, Fresh>, BackgroundThread::PlanRefusal>
plan_in_frame(Marker alloc, DagFrame<Fresh>& frame, ::fixy::View<TensorSlot const> slots,
              BackgroundThread::PlanTarget target = BackgroundThread::PlanTarget{}) noexcept {
    auto slots_run = frame.template build_array<TensorSlot>(
        alloc, ::fixy::ElementCount{slots.size()}, [slots](::fixy::RunStart at) noexcept {
            return *slots.get(at.value().raw()).expect("a position is below the count of the slots");
        });
    const DagFrameRef<MemoryPlan, Fresh> plan = frame.template make<MemoryPlan>(alloc);
    PlanScratch scratch{::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16}};
    ::fixy::Option<BackgroundThread::PlanRefusal> refusal =
        scratch.arena.with_epoch([alloc, target, plan_storage = plan.weaken(),
                                  slot_run = slots_run.weaken()](PlanScratch::Epoch& epoch) mutable noexcept {
            return BackgroundThread::compute_memory_plan(alloc, target, plan_storage, epoch, slot_run).err();
        });
    if (refusal.is_some()) {
        return ::fixy::err(static_cast<::fixy::Option<BackgroundThread::PlanRefusal>&&>(refusal).expect(
            "the test above found the refusal"));
    }
    frame.store(plan, &MemoryPlan::slots, slots_run);
    return plan;
}

// A plan of a copy of slots with the offsets that the slots hold, the pool
// size and the count of external slots, in one frame on the arena.  The pool
// allocator reads such a plan as it reads a plan of the planner.  The plan
// lives as long as the arena.  Complexity: O(n) for n slots.
template <class Marker>
[[nodiscard]] DagRef<MemoryPlan const> manual_plan(Marker alloc, ::fixy::BumpArena<DagArena>& arena,
                                                   ::fixy::View<TensorSlot const> slots, std::uint64_t pool_bytes,
                                                   std::uint32_t num_external) noexcept {
    return ::foundation::core::with_arena_frame(arena, [alloc, slots, pool_bytes, num_external](auto& frame) noexcept {
        auto slots_run = frame.template build_array<TensorSlot>(
            alloc, ::fixy::ElementCount{slots.size()}, [slots](::fixy::RunStart at) noexcept {
                return *slots.get(at.value().raw()).expect("a position is below the count of the slots");
            });
        const auto plan = frame.template make<MemoryPlan>(alloc);
        frame.store(plan, &MemoryPlan::slots, slots_run);
        plan->num_external = num_external;
        plan->pool_bytes = pool_bytes;
        plan->device_type = DeviceType::CPU;
        plan->device_idx = 0;
        return DagRef<MemoryPlan const>{plan.weaken()};
    });
}

// The plan of a copy of slots, in one frame on the arena, as plan_in_frame
// gives it.  The plan lives as long as the arena.
template <class Marker>
[[nodiscard]] ::fixy::Result<DagRef<MemoryPlan>, BackgroundThread::PlanRefusal>
plan_in_arena(Marker alloc, ::fixy::BumpArena<DagArena>& arena, ::fixy::View<TensorSlot const> slots,
              BackgroundThread::PlanTarget target = BackgroundThread::PlanTarget{}) noexcept {
    return ::foundation::core::with_arena_frame(arena, [alloc, slots, target](auto& frame) noexcept {
        auto planned = plan_in_frame(alloc, frame, slots, target);
        if (planned.is_err()) {
            return ::fixy::Result<DagRef<MemoryPlan>, BackgroundThread::PlanRefusal>{::fixy::err(
                static_cast<decltype(planned)&&>(planned).err().expect("the test above found the refusal"))};
        }
        return ::fixy::Result<DagRef<MemoryPlan>, BackgroundThread::PlanRefusal>{
            static_cast<decltype(planned)&&>(planned).expect("the test above found the plan").weaken()};
    });
}

}  // namespace crucible::test
