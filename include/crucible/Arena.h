#pragma once

#include <cstddef>
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
    blocks_.push_back(static_cast<char*>(std::malloc(block_size_)));
  }

  ~Arena() {
    for (char* block : blocks_) {
      std::free(block);
    }
  }

  // Non-copyable, non-movable (pointers into arena must remain valid)
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) = delete;
  Arena& operator=(Arena&&) = delete;

  // Allocate `size` bytes with `align` alignment.
  // Returns a pointer guaranteed to be aligned to `align`.
  void* alloc(size_t size, size_t align = alignof(std::max_align_t)) {
    // Align the current offset
    size_t aligned = (offset_ + align - 1) & ~(align - 1);

    if (aligned + size > block_size_) {
      // Current block is full. Allocate a new one.
      // If the request is larger than block_size_, allocate an oversized block.
      size_t new_block_size = (size > block_size_) ? size + align : block_size_;
      blocks_.push_back(static_cast<char*>(std::malloc(new_block_size)));
      block_size_ = (new_block_size > block_size_) ? block_size_ : new_block_size;
      offset_ = 0;
      aligned = 0; // fresh block, already aligned for any type
    }

    void* ptr = blocks_.back() + aligned;
    offset_ = aligned + size;
    return ptr;
  }

  // Typed allocation helper
  template <typename T>
  T* alloc_obj() {
    return static_cast<T*>(alloc(sizeof(T), alignof(T)));
  }

  // Allocate an array of N elements
  template <typename T>
  T* alloc_array(size_t n) {
    if (n == 0) return nullptr;
    return static_cast<T*>(alloc(sizeof(T) * n, alignof(T)));
  }

  // Total bytes allocated across all blocks
  size_t total_allocated() const {
    return (blocks_.size() - 1) * block_size_ + offset_;
  }

 private:
  std::vector<char*> blocks_;
  size_t block_size_;
  size_t offset_;
};

} // namespace crucible
