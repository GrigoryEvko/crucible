#pragma once

#include "Effects.h"
#include "Platform.h"
#include "Saturate.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace crucible {

// Bump-pointer arena allocator. No individual deallocation.
// All memory freed when the Arena is destroyed.
//
// Used for Expr nodes, DAG structures, TraceEntry arrays, and their
// sub-allocations. Since these objects are immutable and live for the
// duration of compilation, arena allocation is ideal: allocation is a
// pointer bump (~2ns), no fragmentation, excellent cache locality,
// trivial cleanup.
//
// Data layout: hot fields (cur_block_, cur_base_, offset_, end_offset_)
// are packed at the top of the struct, within a single cache line.
// The cold vector and block_size_ are after them, accessed only on
// the slow path (block exhaustion).
class CRUCIBLE_OWNER Arena {
 public:
  explicit Arena(size_t block_size = 1 << 20) // 1MB default blocks
      : block_size_(block_size) {
    auto* p = static_cast<char*>(std::malloc(block_size_));
    if (!p) [[unlikely]] std::abort();
    blocks_.push_back(p);
    cur_block_ = p;
    cur_base_ = reinterpret_cast<uintptr_t>(p);
    end_offset_ = block_size_;
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
  // Requires fx::Alloc capability — cannot be called from hot path.
  //
  // Alignment is computed against the absolute address, not the
  // block-relative offset. std::malloc only guarantees
  // alignof(std::max_align_t) = 16B, so larger alignments (64B for
  // cache-line, 256B for PoolAllocator) require address-aware math.
  //
  // Fast path: ~2ns (pointer bump + bitwise AND alignment).
  // Slow path: malloc + vector push (amortized across block_size_ bytes).
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] CRUCIBLE_INLINE
  void* alloc(fx::Alloc, size_t size, size_t align = alignof(std::max_align_t)) CRUCIBLE_LIFETIMEBOUND {
    // Compute aligned offset within the current block.
    // Uses absolute address for correctness with malloc's arbitrary base.
    uintptr_t aligned_addr = (cur_base_ + offset_ + align - 1) & ~(align - 1);
    size_t aligned = aligned_addr - cur_base_;

    if (aligned + size <= end_offset_) [[likely]] {
      // Fast path: fits in current block.
      void* ptr = cur_block_ + aligned;
      offset_ = aligned + size;
      return ptr;
    }

    // Slow path: current block exhausted (or alignment padding pushed past end).
    return alloc_slow_(size, align);
  }

  // Typed allocation helper. For trivially-constructible types, this
  // compiles to exactly the same code as alloc(sizeof(T), alignof(T)).
  template <typename T>
  [[nodiscard]] CRUCIBLE_INLINE T* alloc_obj(fx::Alloc a) CRUCIBLE_LIFETIMEBOUND {
    return static_cast<T*>(alloc(a, sizeof(T), alignof(T)));
  }

  // Allocate an array of N elements. Returns nullptr for n == 0.
  // Uses saturation arithmetic to prevent overflow on sizeof(T) * n.
  template <typename T>
  [[nodiscard]] CRUCIBLE_INLINE T* alloc_array(fx::Alloc a, size_t n) CRUCIBLE_LIFETIMEBOUND {
    if (n == 0) [[unlikely]] return nullptr;
    size_t nbytes = crucible::sat::mul_sat(n, sizeof(T));
    return static_cast<T*>(alloc(a, nbytes, alignof(T)));
  }

  // Copy a null-terminated string into the arena. Returns nullptr for
  // null input. The returned pointer is valid for the lifetime of the
  // Arena. Alignment is 1 (chars don't need alignment padding).
  [[nodiscard]] const char* copy_string(fx::Alloc a, const char* src) CRUCIBLE_LIFETIMEBOUND {
    if (!src) return nullptr;
    size_t len = std::strlen(src) + 1;
    auto* dst = static_cast<char*>(alloc(a, len, 1));
    std::memcpy(dst, src, len);
    return dst;
  }

  // Total bytes allocated across all blocks (approximate: includes
  // alignment padding and abandoned block tails).
  [[nodiscard]] size_t total_allocated() const {
    return (blocks_.size() - 1) * block_size_ + offset_;
  }

 private:
  // Slow path: allocate a new block and retry alignment.
  // Separated from alloc() to keep the fast path's instruction footprint
  // small — fewer i-cache lines, better branch prediction.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[gnu::noinline]]
  void* alloc_slow_(size_t size, size_t align) {
    // Oversized requests get their own block with room for alignment.
    size_t new_size = (size + align > block_size_) ? size + align : block_size_;
    auto* p = static_cast<char*>(std::malloc(new_size));
    if (!p) [[unlikely]] std::abort();
    blocks_.push_back(p);

    // Update cached hot fields for the new block.
    cur_block_ = p;
    cur_base_ = reinterpret_cast<uintptr_t>(p);
    end_offset_ = new_size;

    uintptr_t aligned_addr = (cur_base_ + align - 1) & ~(align - 1);
    size_t aligned = aligned_addr - cur_base_;

    void* ptr = cur_block_ + aligned;
    offset_ = aligned + size;
    return ptr;
  }

  // ── Hot fields (accessed on every alloc, same cache line) ──
  char* cur_block_ = nullptr;        // Current block pointer (avoids blocks_.back())
  uintptr_t cur_base_ = 0;           // reinterpret_cast<uintptr_t>(cur_block_) cached
  size_t offset_ = 0;                // Current offset within cur_block_
  size_t end_offset_ = 0;            // Usable size of current block (== block_size_ for normal blocks)

  // ── Cold fields (accessed only on slow path) ──
  size_t block_size_ = 0;            // Default block size for new allocations
  std::vector<char*> blocks_;        // All blocks (for cleanup in destructor)
};

static_assert(sizeof(Arena) <= 64, "Arena should fit in one cache line (hot fields + vector)");

} // namespace crucible
