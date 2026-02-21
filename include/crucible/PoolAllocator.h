#pragma once

// PoolAllocator: Pre-allocated memory pool for Tier 1 replay.
//
// Takes a completed MemoryPlan (from sweep-line offset assignment)
// and materializes it: one contiguous aligned allocation for the
// entire pool, plus a pre-built pointer table for O(1) slot lookup.
//
// Hot path: slot_ptr(SlotId) → single 8-byte load from ptr_table_.
// Cold path: init() builds the table once per plan.
//
// External slots (params, data loader outputs) are registered
// separately via register_external() — their memory is owned
// elsewhere. Internal slots point into the pool.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace crucible {

struct PoolAllocator {
  PoolAllocator() = default;
  ~PoolAllocator() { destroy(); }

  PoolAllocator(const PoolAllocator&) = delete("pool base address would alias");
  PoolAllocator& operator=(const PoolAllocator&) = delete("pool base address would alias");
  PoolAllocator(PoolAllocator&&) = delete("ptr_table entries point into pool");
  PoolAllocator& operator=(PoolAllocator&&) = delete("ptr_table entries point into pool");

  // ── Init: materialize a MemoryPlan into a live memory pool ──
  //
  // Allocates pool_bytes of 256-byte-aligned memory, builds the
  // pointer table from slot offsets. External slots start as nullptr
  // and must be registered before replay.
  //
  // Aborts on OOM — a Crucible runtime that can't allocate the pool
  // is unrecoverable (the whole point is pre-allocation).
  void init(const MemoryPlan* plan) CRUCIBLE_NO_THREAD_SAFETY {
    assert(plan && "null MemoryPlan");
    assert(!ptr_table_ && "already initialized — call destroy() first");

    num_slots_ = plan->num_slots;
    num_external_ = plan->num_external;
    pool_bytes_ = plan->pool_bytes;

    // Allocate the pool (one contiguous block).
    // aligned_alloc requires size to be a multiple of alignment.
    if (pool_bytes_ > 0) {
      uint64_t alloc_size = (pool_bytes_ + ALIGNMENT - 1) & ~uint64_t(ALIGNMENT - 1);
      pool_ = std::aligned_alloc(ALIGNMENT, alloc_size);
      if (!pool_) [[unlikely]]
        std::abort();
#ifndef NDEBUG
      // Poison with 0xCD — reads of uninitialized pool memory are
      // immediately visible in debuggers and ASan.
      std::memset(pool_, 0xCD, alloc_size);
#endif
    }

    // Allocate the pointer table.
    if (num_slots_ > 0) {
      ptr_table_ = static_cast<void**>(
          std::calloc(num_slots_, sizeof(void*)));
      if (!ptr_table_) [[unlikely]]
        std::abort();

      // Fill internal slots: base + offset.
      // External slots stay nullptr (calloc zeroed them).
      auto* base = static_cast<char*>(pool_);
      for (uint32_t s = 0; s < num_slots_; s++) {
        if (!plan->slots[s].is_external) {
          assert(plan->slots[s].offset_bytes + plan->slots[s].nbytes <= pool_bytes_
                 && "slot exceeds pool bounds");
          assert(plan->slots[s].offset_bytes % ALIGNMENT == 0
                 && "slot offset not aligned — sweep-line bug");
          ptr_table_[s] = base + plan->slots[s].offset_bytes;
        }
      }
    }
  }

  // ── Destroy: free pool and pointer table ──
  void destroy() {
    std::free(pool_);
    std::free(ptr_table_);
    pool_ = nullptr;
    ptr_table_ = nullptr;
    pool_bytes_ = 0;
    num_slots_ = 0;
    num_external_ = 0;
  }

  // ── Hot path: get pointer for a tensor slot ──
  //
  // Returns the data pointer for the given slot. For internal slots,
  // this points into the pre-allocated pool. For external slots,
  // this returns whatever was registered via register_external().
  //
  // Single 8-byte load — the entire point of pre-building the table.
  [[nodiscard]] CRUCIBLE_INLINE void* slot_ptr(SlotId sid) const {
    assert(sid.raw() < num_slots_ && "SlotId out of bounds");
    return ptr_table_[sid.raw()];
  }

  // ── Register an external tensor's pointer ──
  //
  // External slots (params, data loader outputs, optimizer states)
  // keep their existing allocations. The Vessel adapter calls this
  // before replay begins for each external slot.
  void register_external(SlotId sid, void* ptr) {
    assert(sid.raw() < num_slots_ && "SlotId out of bounds");
    assert(ptr && "registering null external pointer");
    ptr_table_[sid.raw()] = ptr;
  }

  // ── Raw table access for hot inner loops ──
  //
  // Returns the raw pointer table so callers (ReplayEngine) can
  // capture it into a local, eliminating a level of indirection:
  //
  //   void* const* tbl = pool.table();
  //   for (...) { void* p = tbl[sid.raw()]; }  // one load per call
  //
  // vs the two-load path through slot_ptr() when the compiler cannot
  // prove that ptr_table_ doesn't change across loop iterations.
  [[nodiscard]] CRUCIBLE_INLINE void* const* table() const {
    return ptr_table_;
  }

  // ── Queries ──
  [[nodiscard]] void* pool_base() const { return pool_; }
  [[nodiscard]] uint64_t pool_bytes() const { return pool_bytes_; }
  [[nodiscard]] uint32_t num_slots() const { return num_slots_; }
  [[nodiscard]] uint32_t num_external() const { return num_external_; }
  [[nodiscard]] bool is_initialized() const { return ptr_table_ != nullptr; }

  // 256-byte alignment: matches compute_memory_plan() in BackgroundThread.h.
  // Critical for CUDA coalescing and AVX-512 vector loads.
  static constexpr uint32_t ALIGNMENT = 256;

  // ── DetachedPool: RAII handle for a pool buffer detached from this allocator ──
  //
  // Returned by detach().  Owns the raw aligned allocation and frees it
  // on destruction.  Non-copyable, non-movable — consumed at the detach
  // site via guaranteed copy elision (C++17 P0135).
  struct DetachedPool {
    void* base = nullptr;
    uint64_t bytes = 0;

    DetachedPool() = default;
    DetachedPool(void* b, uint64_t n) : base(b), bytes(n) {}
    ~DetachedPool() { std::free(base); }

    DetachedPool(const DetachedPool&)            = delete("would double-free the pool buffer");
    DetachedPool& operator=(const DetachedPool&) = delete("would double-free the pool buffer");
    DetachedPool(DetachedPool&&)                 = delete("consumed at detach site via guaranteed elision");
    DetachedPool& operator=(DetachedPool&&)      = delete("consumed at detach site via guaranteed elision");
  };

  // ── Detach: release ownership of the pool buffer ──
  //
  // Returns a DetachedPool holding the raw allocation.  The allocator
  // is reset to empty (ptr_table freed, counters zeroed).  The caller
  // can read from the detached buffer, then let it destruct to free.
  //
  // Used by CrucibleContext::switch_region() to keep old pool data
  // alive while initializing a new pool for the alternate region.
  [[nodiscard]] DetachedPool detach() {
    assert(pool_ && "detaching an uninitialized pool");
    void* p = pool_;
    uint64_t n = pool_bytes_;
    pool_ = nullptr;   // prevent destroy() from freeing the buffer
    destroy();          // frees ptr_table_, zeros counters
    return DetachedPool{p, n};  // prvalue: guaranteed copy elision (P0135)
  }

 private:
  void* pool_ = nullptr;           // one big aligned allocation
  void** ptr_table_ = nullptr;     // SlotId → data pointer (pre-built)
  uint64_t pool_bytes_ = 0;        // pool size
  uint32_t num_slots_ = 0;         // table size
  uint32_t num_external_ = 0;      // external slot count (for diagnostics)
};

static_assert(sizeof(PoolAllocator) == 32, "PoolAllocator layout: 2 ptrs + u64 + 2×u32");

} // namespace crucible
