#pragma once

// PoolAllocator: materialize a MemoryPlan into one aligned heap allocation
// plus a SlotId→void* table. Hot path (slot_ptr) is a single load; init /
// destroy / detach are cold.
//
// External slots (params, loader outputs, optimizer state) are owned by the
// Vessel and registered via register_external(); internal slots point into
// the pool. The pool and table are raw heap (not Linear<T>) because detach()
// transfers ownership of the pool buffer out — Linear<T>'s move-consume
// would couple buffer lifetime to this struct, defeating that use case.
//
// Ownership: constructed and written by the background thread; read-only
// from the foreground replay after init returns. Not movable — ptr_table_
// entries are interior pointers into pool_.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/rt/Registry.h>
#include <crucible/safety/Checked.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace crucible {

struct CRUCIBLE_OWNER PoolAllocator {
  PoolAllocator() = default;
  ~PoolAllocator() { destroy(); }

  PoolAllocator(const PoolAllocator&)            = delete("interior pointers into pool_ would alias");
  PoolAllocator& operator=(const PoolAllocator&) = delete("interior pointers into pool_ would alias");
  PoolAllocator(PoolAllocator&&)                 = delete("ptr_table_ entries are interior pointers into pool_");
  PoolAllocator& operator=(PoolAllocator&&)      = delete("ptr_table_ entries are interior pointers into pool_");

  // 256B: matches compute_memory_plan() offsets; required for CUDA
  // coalescing and AVX-512 vector loads.
  static constexpr uint32_t ALIGNMENT = 256;

  // DetachedPool — raw aligned buffer owned by the detach site; frees on
  // destruction. Non-copyable, non-movable: consumed via guaranteed copy
  // elision (C++17 P0135).
  struct CRUCIBLE_OWNER DetachedPool {
    void*    base  = nullptr;
    uint64_t bytes = 0;

    DetachedPool() = default;
    DetachedPool(void* b, uint64_t n) noexcept : base{b}, bytes{n} {}
    ~DetachedPool() { std::free(base); }

    DetachedPool(const DetachedPool&)            = delete("would double-free the pool buffer");
    DetachedPool& operator=(const DetachedPool&) = delete("would double-free the pool buffer");
    DetachedPool(DetachedPool&&)                 = delete("consumed at detach site via guaranteed elision");
    DetachedPool& operator=(DetachedPool&&)      = delete("consumed at detach site via guaranteed elision");
  };

  // Materialize a plan. Aborts on OOM — a pre-allocated runtime that can't
  // allocate the plan has no recovery path. External slots start null and
  // must be registered before replay. noexcept: failure mode is abort, not throw.
  [[gnu::cold, gnu::noinline]]
  void init(const MemoryPlan* plan) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (plan        != nullptr)
      pre (ptr_table_  == nullptr)   // double-init forbidden
      pre (pool_       == nullptr)
  {
    num_slots_    = plan->num_slots;
    num_external_ = plan->num_external;
    pool_bytes_   = plan->pool_bytes;

    if (pool_bytes_ > 0) {
      // 2 MB alignment when pool ≥ 2 MB (THP-eligible); else 256 B floor.
      const size_t page_align =
          (pool_bytes_ >= crucible::rt::kHugePageBytes)
              ? crucible::rt::kHugePageBytes : ALIGNMENT;
      const uint64_t padded     = safety::saturating_add<uint64_t>(
          pool_bytes_, page_align - 1);
      const uint64_t alloc_size = padded & ~(page_align - 1);
      pool_ = std::aligned_alloc(page_align, alloc_size);
      if (!pool_) [[unlikely]] std::abort();
#ifndef NDEBUG
      std::memset(pool_, 0xCD, alloc_size);  // 0xCD: uninit reads visible
#endif
      const bool huge = (page_align == crucible::rt::kHugePageBytes);
      crucible::rt::register_hot_region(pool_, alloc_size,
          /*huge=*/huge, "PoolAllocator.pool");
    }

    if (num_slots_ > 0) {
      ptr_table_ = static_cast<void**>(std::calloc(num_slots_, sizeof(void*)));
      if (!ptr_table_) [[unlikely]] std::abort();

      // Internal slots: base + offset. External slots stay null (calloc'd).
      auto* base = static_cast<char*>(pool_);
      for (uint32_t s = 0; s < num_slots_; ++s) {
        const auto& slot = plan->slots[s];
        if (!slot.is_external) {
          assert(slot.offset_bytes + slot.nbytes <= pool_bytes_ &&
                 "slot exceeds pool bounds");
          assert(slot.offset_bytes % ALIGNMENT == 0 &&
                 "slot offset not ALIGNMENT-aligned — sweep-line bug");
          ptr_table_[s] = base + slot.offset_bytes;
        }
      }
    }
  }

  [[gnu::cold]]
  void destroy() noexcept {
    if (pool_) crucible::rt::unregister_hot_region(pool_);
    std::free(pool_);
    std::free(ptr_table_);
    pool_         = nullptr;
    ptr_table_    = nullptr;
    pool_bytes_   = 0;
    num_slots_    = 0;
    num_external_ = 0;
  }

  // Hot path: single 8-byte load. Returns null for unregistered externals.
  [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
  inline void* slot_ptr(SlotId sid) const noexcept CRUCIBLE_LIFETIMEBOUND
      pre (sid.raw() < num_slots_)
  {
    return ptr_table_[sid.raw()];
  }

  // Wire an external tensor into its slot. Called by the Vessel once per
  // external slot before replay begins.
  void register_external(SlotId sid, void* ptr) noexcept
      pre (sid.raw() < num_slots_)
      pre (ptr       != nullptr)
      pre (ptr_table_ != nullptr)
  {
    ptr_table_[sid.raw()] = ptr;
  }

  // Raw table for inner loops that want to hoist the indirection:
  //   void* const* tbl = pool.table();
  //   for (...) p = tbl[sid.raw()];   // one load per access
  [[nodiscard, gnu::pure]] CRUCIBLE_INLINE
  void* const* table() const noexcept CRUCIBLE_LIFETIMEBOUND {
    return ptr_table_;
  }

  [[nodiscard, gnu::pure]] void*    pool_base()      const noexcept CRUCIBLE_LIFETIMEBOUND { return pool_; }
  [[nodiscard, gnu::pure]] uint64_t pool_bytes()     const noexcept { return pool_bytes_; }
  [[nodiscard, gnu::pure]] uint32_t num_slots()      const noexcept { return num_slots_; }
  [[nodiscard, gnu::pure]] uint32_t num_external()   const noexcept { return num_external_; }
  [[nodiscard, gnu::pure]] bool     is_initialized() const noexcept { return ptr_table_ != nullptr; }

  // Release pool_ to a DetachedPool, then reset this allocator to empty.
  // Used by CrucibleContext::switch_region() to keep old pool data alive
  // while initializing the alternate region.
  [[nodiscard, gnu::cold]]
  DetachedPool detach() noexcept
      pre (pool_ != nullptr)
  {
    void* p          = pool_;
    uint64_t n       = pool_bytes_;
    crucible::rt::unregister_hot_region(p);
    pool_            = nullptr;   // destroy() skips free since pool_==nullptr
    destroy();
    return DetachedPool{p, n};
  }

 private:
  // Raw pointers (not Linear<T>) because detach() moves pool_ out to a
  // separate RAII sink; Linear<T>'s consume-on-move would couple the
  // buffer's lifetime to the allocator instance.
  void*    pool_         = nullptr;   // one aligned allocation, pool_bytes_ long
  void**   ptr_table_    = nullptr;   // SlotId.raw() → data pointer
  uint64_t pool_bytes_   = 0;
  uint32_t num_slots_    = 0;
  uint32_t num_external_ = 0;
};

static_assert(sizeof(PoolAllocator) == 32, "PoolAllocator layout: 2 ptrs + u64 + 2*u32");
static_assert(alignof(PoolAllocator) == 8);

} // namespace crucible
