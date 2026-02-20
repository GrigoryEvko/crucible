#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace crucible {

// Bump-pointer arena allocator. No individual deallocation.
// All memory freed when the Arena is destroyed.
//
// Used for Expr nodes and their args arrays. Since expressions are
// immutable and live for the duration of compilation, arena allocation
// is ideal: allocation is a pointer bump (~2ns), no fragmentation,
// excellent cache locality, trivial cleanup.
class Arena {
 public:
  explicit Arena(size_t block_size = 1 << 20) // 1MB default blocks
      : block_size_(block_size), offset_(0) {
    auto* p = static_cast<char*>(std::malloc(block_size_));
    if (!p) [[unlikely]] std::abort();
    blocks_.push_back(p);
  }

  ~Arena() {
    for (char* block : blocks_) {
      std::free(block);
    }
  }

  // Non-copyable, non-movable (pointers into arena must remain valid)
  Arena(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
  Arena& operator=(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
  Arena(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");
  Arena& operator=(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");

  // Allocate `size` bytes with `align` alignment.
  // Returns a pointer guaranteed to be aligned to `align`.
  //
  // Alignment is computed against the absolute address, not the
  // block-relative offset. std::malloc only guarantees
  // alignof(std::max_align_t) = 16B, so larger alignments (64B for
  // cache-line, 256B for PoolAllocator) require address-aware math.
  [[nodiscard]] void* alloc(size_t size, size_t align = alignof(std::max_align_t)) {
    uintptr_t base = reinterpret_cast<uintptr_t>(blocks_.back());
    uintptr_t aligned_addr = (base + offset_ + align - 1) & ~(align - 1);
    size_t aligned = static_cast<size_t>(aligned_addr - base);

    if (aligned + size > block_size_) {
      // Current block exhausted (or alignment padding pushed past end).
      // Oversized requests get their own block with room for alignment.
      size_t new_size = (size + align > block_size_) ? size + align : block_size_;
      auto* p = static_cast<char*>(std::malloc(new_size));
      if (!p) [[unlikely]] std::abort();
      blocks_.push_back(p);
      offset_ = 0;

      // Re-compute alignment for the fresh block.
      base = reinterpret_cast<uintptr_t>(p);
      aligned_addr = (base + align - 1) & ~(align - 1);
      aligned = static_cast<size_t>(aligned_addr - base);
    }

    void* ptr = blocks_.back() + aligned;
    offset_ = aligned + size;
    return ptr;
  }

  // Typed allocation helper
  template <typename T>
  [[nodiscard]] T* alloc_obj() {
    return static_cast<T*>(alloc(sizeof(T), alignof(T)));
  }

  // Allocate an array of N elements
  template <typename T>
  [[nodiscard]] T* alloc_array(size_t n) {
    if (n == 0) return nullptr;
    return static_cast<T*>(alloc(sizeof(T) * n, alignof(T)));
  }

  // Total bytes allocated across all blocks
  [[nodiscard]] size_t total_allocated() const {
    return (blocks_.size() - 1) * block_size_ + offset_;
  }

 private:
  std::vector<char*> blocks_;
  size_t block_size_;
  size_t offset_;
};

} // namespace crucible
