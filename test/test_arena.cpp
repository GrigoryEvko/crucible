#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

using crucible::safety::Positive;
using crucible::safety::PowerOfTwo;

int main() {
  crucible::effects::Test test;

  // Basic allocation
  crucible::Arena arena(4096);
  int* a = arena.alloc_obj<int>(test.alloc);
  *a = 42;
  assert(*a == 42);

  // Array allocation
  double* arr = arena.alloc_array<double>(test.alloc, 100);
  for (int i = 0; i < 100; i++) arr[i] = i * 1.5;
  assert(arr[99] >= 148.4 && arr[99] <= 148.6);  // 99 * 1.5 = 148.5

  // Alignment: 16-byte aligned allocation (max_align_t guarantee)
  void* aligned = arena.alloc(test.alloc, Positive<size_t>{128}, PowerOfTwo<size_t>{16});
  assert(reinterpret_cast<uintptr_t>(aligned) % 16 == 0);

  // Cross-block allocation (force new block)
  crucible::Arena small_arena(64);
  char* p1 = static_cast<char*>(small_arena.alloc(test.alloc, Positive<size_t>{32}, PowerOfTwo<size_t>{1}));
  char* p2 = static_cast<char*>(small_arena.alloc(test.alloc, Positive<size_t>{64}, PowerOfTwo<size_t>{1}));
  // p2 must be in a new block (32 + 64 > 64)
  assert(p1 != nullptr);
  assert(p2 != nullptr);

  // Oversized allocation (larger than block size)
  crucible::Arena tiny_arena(32);
  void* big = tiny_arena.alloc(test.alloc, Positive<size_t>{1024}, PowerOfTwo<size_t>{1});
  assert(big != nullptr);

  // Zero-length array returns nullptr
  int* empty = tiny_arena.alloc_array<int>(test.alloc, 0);
  assert(empty == nullptr);

  // total_allocated grows
  crucible::Arena tracker(1024);
  size_t before = tracker.total_allocated();
  (void)tracker.alloc(test.alloc, Positive<size_t>{256}, PowerOfTwo<size_t>{1});
  assert(tracker.total_allocated() > before);

  // Oversized allocation is fully accounted by total_allocated().
  // The old implementation used (blocks-1)*block_size + offset which
  // undercounts dedicated blocks sized above block_size_.
  {
    crucible::Arena oversized(64);
    (void)oversized.alloc(test.alloc, Positive<size_t>{1 << 20}, PowerOfTwo<size_t>{1});  // 1MB in a 64B arena
    assert(oversized.total_allocated() >= (1 << 20));
    assert(oversized.block_count() == 2);  // initial 64B + one 1MB
  }

  // Second oversized block in a row still increments correctly.
  {
    crucible::Arena multi(32);
    (void)multi.alloc(test.alloc, Positive<size_t>{4096}, PowerOfTwo<size_t>{1});
    (void)multi.alloc(test.alloc, Positive<size_t>{8192}, PowerOfTwo<size_t>{1});
    assert(multi.total_allocated() >= 4096 + 8192);
    assert(multi.block_count() == 3);
  }

  // Fast-path byte-accounting: total_allocated equals the sum of bump
  // offsets when no slow path fires.
  {
    crucible::Arena steady(1 << 16);
    (void)steady.alloc(test.alloc, Positive<size_t>{100}, PowerOfTwo<size_t>{1});
    (void)steady.alloc(test.alloc, Positive<size_t>{200}, PowerOfTwo<size_t>{1});
    (void)steady.alloc(test.alloc, Positive<size_t>{300}, PowerOfTwo<size_t>{1});
    assert(steady.total_allocated() == 600);
    assert(steady.block_count() == 1);
  }

  // alloc_array<T>(0) returns nullptr AND doesn't consume any bytes.
  {
    crucible::Arena pristine(1024);
    size_t bytes_before = pristine.total_allocated();
    int* zero = pristine.alloc_array<int>(test.alloc, 0);
    assert(zero == nullptr);
    assert(pristine.total_allocated() == bytes_before);
  }

  // copy_string(nullptr) returns nullptr without allocating.
  {
    crucible::Arena s(1024);
    size_t bytes_before = s.total_allocated();
    const char* none = s.copy_string(test.alloc, nullptr);
    assert(none == nullptr);
    assert(s.total_allocated() == bytes_before);

    // Non-null source round-trips byte-identically.
    const char* src = "crucible";
    const char* dst = s.copy_string(test.alloc, src);
    assert(dst != nullptr);
    assert(std::strcmp(dst, src) == 0);
    assert(dst != src);  // distinct storage
  }

  // Alignment honored for all power-of-two requests up to 256.
  {
    crucible::Arena align_arena(1 << 16);
    for (size_t align : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u}) {
      void* p = align_arena.alloc(test.alloc, Positive<size_t>{8}, PowerOfTwo<size_t>{align});
      assert(reinterpret_cast<uintptr_t>(p) % align == 0);
    }
  }

  std::printf("test_arena: all tests passed\n");
  return 0;
}
