#include <crucible/MerkleDag.h>
#include <crucible/BackgroundThread.h>
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  crucible::BackgroundThread bt;

  // Create slots with known liveness intervals for memory planning.
  // Slot 0: birth=0, death=5, 1024 bytes
  // Slot 1: birth=1, death=3, 2048 bytes
  // Slot 2: birth=4, death=7, 1024 bytes (should reuse slot 1's space)
  // Slot 3: birth=0, death=7, 512 bytes, external (not planned)
  constexpr uint32_t N = 4;
  crucible::TensorSlot slots[N];
  std::memset(slots, 0, sizeof(slots));

  using crucible::SlotId; using crucible::OpIndex;
  using crucible::ScalarType; using crucible::DeviceType; using crucible::Layout;
  slots[0] = {0, 1024, SlotId{0}, OpIndex{0}, OpIndex{5}, ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
  slots[1] = {0, 2048, SlotId{1}, OpIndex{1}, OpIndex{3}, ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
  slots[2] = {0, 1024, SlotId{2}, OpIndex{4}, OpIndex{7}, ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}};
  slots[3] = {0, 512,  SlotId{3}, OpIndex{0}, OpIndex{7}, ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true,  {}};

  auto* plan = bt.compute_memory_plan(slots, N);
  assert(plan != nullptr);
  assert(plan->num_slots == N);
  assert(plan->num_external == 1);
  assert(plan->pool_bytes > 0);

  // External slot should NOT have an assigned offset (it keeps 0)
  // Internal slots should have valid offsets
  // Slot 2 should reuse space from slot 1 (slot 1 dies at 3, slot 2 born at 4)
  assert(slots[2].offset_bytes <= slots[1].offset_bytes + 2048);

  // Pool should be smaller than sum of all internal slots (due to reuse)
  uint64_t total_internal = 1024 + 2048 + 1024;
  assert(plan->pool_bytes < total_internal);

  // No overlapping live ranges should have overlapping offsets.
  // Slots 0 and 1 are both alive at op 1, so they must not overlap.
  uint64_t s0_end = slots[0].offset_bytes + 1024;
  uint64_t s1_start = slots[1].offset_bytes;
  uint64_t s1_end = slots[1].offset_bytes + 2048;
  uint64_t s0_start = slots[0].offset_bytes;
  bool no_overlap_01 = (s0_end <= s1_start) || (s1_end <= s0_start);
  assert(no_overlap_01);

  std::printf("test_memory_plan: all tests passed\n");
  std::printf("  pool_bytes: %lu\n", static_cast<unsigned long>(plan->pool_bytes));
  std::printf("  slot offsets: [%lu, %lu, %lu, %lu]\n",
              static_cast<unsigned long>(slots[0].offset_bytes),
              static_cast<unsigned long>(slots[1].offset_bytes),
              static_cast<unsigned long>(slots[2].offset_bytes),
              static_cast<unsigned long>(slots[3].offset_bytes));
  return 0;
}
