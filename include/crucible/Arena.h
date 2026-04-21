#pragma once

// Arena: bump-pointer allocator with block growth. No individual free.
// All memory returned to the OS on destruction.
//
// Fast path ~2ns (pointer bump + AND mask). Slow path malloc + vector push,
// amortized across block_size_ bytes.
//
// Hot fields (cur_block_, offset_, end_offset_) packed at struct head in one
// cache line; cold fields (block_size_, total_block_bytes_, blocks_) follow,
// touched only on slow path or total_allocated() query. sizeof(Arena) == 64.

#include "Effects.h"
#include "Platform.h"
#include "Saturate.h"
#include "safety/Mutation.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace crucible {

class CRUCIBLE_OWNER Arena {
 public:
  // block_size: default bump-block size (must be > 0). Individual allocations
  // larger than block_size get their own dedicated block.
  explicit Arena(size_t block_size = size_t{1} << 20)
      pre (block_size > 0)
      : block_size_{block_size} {
    alloc_new_block_(block_size_);
  }

  ~Arena() {
    for (char* block : blocks_) std::free(block);
  }

  // Interior pointers returned by alloc() must remain valid for the Arena's
  // lifetime; copying or moving would invalidate them.
  Arena(const Arena&)            = delete("Arena is non-copyable: interior pointers would dangle");
  Arena& operator=(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
  Arena(Arena&&)                 = delete("Arena is non-movable: interior pointers would dangle");
  Arena& operator=(Arena&&)      = delete("Arena is non-movable: interior pointers would dangle");

  // Allocate `size` bytes aligned to `align`. `align` must be a power of two.
  // Returns a non-null pointer valid for the Arena's lifetime; OOM aborts.
  //
  // Alignment uses absolute address arithmetic because malloc only guarantees
  // alignof(std::max_align_t) = 16B; larger alignments (64B cache-line, 256B
  // PoolAllocator) cannot assume block-relative offset suffices.
  //
  // alloc_size/alloc_align indices are 1-based and count the implicit `this`
  // for non-static members (this=1, fx::Alloc=2, size=3, align=4). `malloc`
  // promises no alias with any existing allocation.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::malloc, gnu::returns_nonnull, gnu::alloc_size(3), gnu::alloc_align(4)]]
  CRUCIBLE_INLINE
  void* alloc(fx::Alloc, size_t size, size_t align = alignof(std::max_align_t)) CRUCIBLE_LIFETIMEBOUND
      pre (std::has_single_bit(align))
      pre (size > 0)
  {
    const uintptr_t base = std::bit_cast<uintptr_t>(cur_block_);
    const uintptr_t aligned_addr = (base + offset_ + align - 1) & ~(align - 1);
    const size_t aligned = aligned_addr - base;

    if (aligned + size <= end_offset_) [[likely]] {
      void* ptr = cur_block_ + aligned;
      offset_ = aligned + size;
      return ptr;
    }
    return alloc_slow_(size, align);
  }

  // Allocate a single default-constructible T. Storage only — caller must
  // placement-new if T requires construction. For trivial T this compiles to
  // the same code as alloc(sizeof(T), alignof(T)).
  template <typename T>
  [[nodiscard, gnu::returns_nonnull]] CRUCIBLE_INLINE
  T* alloc_obj(fx::Alloc a) CRUCIBLE_LIFETIMEBOUND {
    return static_cast<T*>(alloc(a, sizeof(T), alignof(T)));
  }

  // Allocate N elements of T. n == 0 returns nullptr (paired with count=0 at
  // call sites per NullSafe axiom). Overflow in sizeof(T)*n saturates to
  // SIZE_MAX, forcing the downstream malloc to fail and std::abort cleanly.
  template <typename T>
  [[nodiscard]] CRUCIBLE_INLINE
  T* alloc_array(fx::Alloc a, size_t n) CRUCIBLE_LIFETIMEBOUND {
    if (n == 0) [[unlikely]] return nullptr;
    const size_t nbytes = crucible::sat::mul_sat(n, sizeof(T));
    return static_cast<T*>(alloc(a, nbytes, alignof(T)));
  }

  // Copy a null-terminated string into the arena. Returns nullptr iff src is
  // null, non-null otherwise (NullSafe: both pointer and length agree).
  [[nodiscard]] const char* copy_string(fx::Alloc a, const char* src) CRUCIBLE_LIFETIMEBOUND {
    if (src == nullptr) return nullptr;
    const size_t len = std::strlen(src) + 1;
    auto* dst = static_cast<char*>(alloc(a, len, 1));
    std::memcpy(dst, src, len);
    return dst;
  }

  // Total bytes allocated by this arena across all blocks, counting the full
  // size of oversized blocks (>block_size_) and excluding only the unused
  // tail of the current block. A single 1MB request in a 32B arena reports
  // ~1MB, not 32B. Used by ExprPool::bytes_used() and test assertions.
  //
  // Invariant: total_block_bytes_ >= end_offset_ >= offset_ (each new block
  // adds its exact size to the total; offset_ never exceeds end_offset_),
  // so the subtraction below cannot underflow.
  [[nodiscard, gnu::pure]] size_t total_allocated() const noexcept {
    return total_block_bytes_.get() - (end_offset_ - offset_);
  }

  // Number of blocks currently held. Diagnostic only.
  [[nodiscard, gnu::pure]] size_t block_count() const noexcept {
    return blocks_.size();
  }

 private:
  // Slow path separated to keep the fast path's instruction footprint tight.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[gnu::noinline, gnu::cold, gnu::returns_nonnull]]
  void* alloc_slow_(size_t size, size_t align) {
    // An oversized request (size + align padding > block_size_) gets its own
    // block sized for it. Saturating add ensures size ≈ SIZE_MAX triggers an
    // abort via malloc failure rather than wrapping to a tiny block.
    const size_t needed = crucible::sat::add_sat(size, align);
    const size_t new_size = (needed > block_size_) ? needed : block_size_;
    alloc_new_block_(new_size);

    const uintptr_t base = std::bit_cast<uintptr_t>(cur_block_);
    const uintptr_t aligned_addr = (base + align - 1) & ~(align - 1);
    const size_t aligned = aligned_addr - base;

    void* ptr = cur_block_ + aligned;
    offset_ = aligned + size;
    return ptr;
  }

  // Establish a fresh block of exactly `nbytes` as the current bump region.
  // Updates cached hot fields and the running byte-count total.
  [[gnu::cold]]
  void alloc_new_block_(size_t nbytes)
      pre (nbytes > 0)
  {
    auto* p = static_cast<char*>(std::malloc(nbytes));
    if (p == nullptr) [[unlikely]] std::abort();
    blocks_.push_back(p);

    cur_block_  = p;
    offset_     = 0;
    end_offset_ = nbytes;

    // Saturating add against pathological totals; clamping here keeps
    // total_allocated() sane even under adversarial tests.  advance() is
    // monotonicity-checked: saturating_add never decreases, so the contract
    // holds by construction.
    total_block_bytes_.advance(
        crucible::sat::add_sat(total_block_bytes_.get(), nbytes));
  }

  // Hot fields (one cache line, touched on every alloc).
  char*  cur_block_  = nullptr;
  size_t offset_     = 0;
  size_t end_offset_ = 0;

  // Cold fields (slow path / diagnostic only).
  size_t                              block_size_        = 0;
  crucible::safety::Monotonic<size_t> total_block_bytes_ {0};
  std::vector<char*>                  blocks_            {};
};

static_assert(sizeof(Arena) == 64, "Arena must fit within one cache line");

} // namespace crucible
