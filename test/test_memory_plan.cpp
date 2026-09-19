#include <crucible/MerkleDag.h>
#include <crucible/BackgroundThread.h>
#include <crucible/effects/_Capabilities.h>
#include "test_assert.h"
#include <cstdio>
#include <cstring>

int main() {
    auto test = crucible::effects::testing::test();
    crucible::BackgroundThread bt;

    // The intervals are chosen so slot 2 can reuse slot 1's space: slot 1 dies at
    // op 3 and slot 2 is born at op 4.  Slot 3 is external and takes no pool
    // space.
    constexpr uint32_t N = 4;
    crucible::TensorSlot slots[N]{};

    using crucible::SlotId;
    using crucible::OpIndex;
    using crucible::ScalarType;
    using crucible::DeviceType;
    using crucible::Layout;
    slots[0] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{5},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 0,
                .nbytes = 2048,
                .birth_op = OpIndex{1},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};
    slots[2] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{4},
                .death_op = OpIndex{7},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{2},
                .pad2 = {}};
    slots[3] = {.offset_bytes = 0,
                .nbytes = 512,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{7},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{3},
                .pad2 = {}};

    auto* plan = bt.compute_memory_plan(test.alloc, slots, N);
    assert(plan != nullptr);
    assert(plan->num_slots == N);
    assert(plan->num_external == 1);
    assert(plan->pool_bytes > 0);

    assert(slots[2].offset_bytes <= slots[1].offset_bytes + 2048);

    uint64_t total_internal = 1024 + 2048 + 1024;
    assert(plan->pool_bytes < total_internal);

    // At every op boundary the simultaneously-live slots must hold pairwise
    // disjoint byte intervals.  The loop runs to 7 because that is the largest
    // death_op among the slots.  The helper excludes external slots, so only the
    // internal live set is checked.
    using crucible::live_intervals_disjoint_at;
    std::span<const crucible::TensorSlot> slot_span{slots, N};
    for (uint32_t t = 0; t <= 7; ++t) {
        bool disjoint = live_intervals_disjoint_at<4>(slot_span, OpIndex{t});
        assert(disjoint);
    }

    std::printf("test_memory_plan: all tests passed\n");
    std::printf("  pool_bytes: %lu\n", static_cast<unsigned long>(plan->pool_bytes));
    std::printf("  slot offsets: [%lu, %lu, %lu, %lu]\n", static_cast<unsigned long>(slots[0].offset_bytes),
                static_cast<unsigned long>(slots[1].offset_bytes), static_cast<unsigned long>(slots[2].offset_bytes),
                static_cast<unsigned long>(slots[3].offset_bytes));
    return 0;
}
