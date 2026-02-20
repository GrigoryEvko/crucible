#include <crucible/Arena.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  // Basic allocation
  crucible::Arena arena(4096);
  int* a = arena.alloc_obj<int>();
  *a = 42;
  assert(*a == 42);

  // Array allocation
  double* arr = arena.alloc_array<double>(100);
  for (int i = 0; i < 100; i++) arr[i] = i * 1.5;
  assert(arr[99] >= 148.4 && arr[99] <= 148.6);  // 99 * 1.5 = 148.5

  // Alignment: 16-byte aligned allocation (max_align_t guarantee)
  void* aligned = arena.alloc(128, 16);
  assert(reinterpret_cast<uintptr_t>(aligned) % 16 == 0);

  // Cross-block allocation (force new block)
  crucible::Arena small_arena(64);
  char* p1 = static_cast<char*>(small_arena.alloc(32, 1));
  char* p2 = static_cast<char*>(small_arena.alloc(64, 1));
  // p2 must be in a new block (32 + 64 > 64)
  assert(p1 != nullptr);
  assert(p2 != nullptr);

  // Oversized allocation (larger than block size)
  crucible::Arena tiny_arena(32);
  void* big = tiny_arena.alloc(1024, 1);
  assert(big != nullptr);

  // Zero-length array returns nullptr
  int* empty = tiny_arena.alloc_array<int>(0);
  assert(empty == nullptr);

  // total_allocated grows
  crucible::Arena tracker(1024);
  size_t before = tracker.total_allocated();
  (void)tracker.alloc(256, 1);
  assert(tracker.total_allocated() > before);

  std::printf("test_arena: all tests passed\n");
  return 0;
}
