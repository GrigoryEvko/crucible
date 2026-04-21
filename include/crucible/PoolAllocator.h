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
#include <crucible/safety/Refined.h>
#include <crucible/safety/ScopedView.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace crucible {

// ── PoolAllocator state tag ─────────────────────────────────────────
// Used with safety::ScopedView to prove at compile time that the
// allocator is in the Initialized state before slot_ptr / register_
// external / detach are called.  Empty is implicitly the negation —
// the only Empty-only public method is init(), which is a member
// function and doesn't need an external view.
namespace pool_state {
    struct Initialized {};   // init() succeeded, destroy() not yet called
}

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
  struct [[nodiscard]] CRUCIBLE_OWNER DetachedPool {
    void*    base  = nullptr;
    uint64_t bytes = 0;

    DetachedPool() = default;
    // Owner ctor — carries a freshly-allocated buffer.  [[nodiscard]] at
    // class level already prevents discarding the whole DetachedPool;
    // the ctor doesn't need its own attribute but the class-level
    // nodiscard makes accidental temporaries at the detach site a
    // compile error — the caller must bind to a named local.
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
      //
      // Overflow-checked bounds verification: an adversarial MemoryPlan
      // with offset_bytes = UINT64_MAX - 100 and nbytes = 200 would
      // produce offset+nbytes ≈ 100 — smaller than pool_bytes_, so the
      // old `offset + nbytes <= pool_bytes` assertion passed while the
      // slot actually wrapped past the end of the pool and into the
      // allocator's metadata.  __builtin_add_overflow catches the wrap
      // deterministically; the subsequent <= check then compares a real
      // end offset.
      auto* base = static_cast<char*>(pool_);
      for (uint32_t s = 0; s < num_slots_; ++s) {
        const auto& slot = plan->slots[s];
        if (!slot.is_external) {
          uint64_t end_offset;
          if (__builtin_add_overflow(slot.offset_bytes, slot.nbytes,
                                     &end_offset)) [[unlikely]] {
            std::fprintf(stderr,
                "PoolAllocator: slot %u offset_bytes+nbytes overflow "
                "(offset=%llu nbytes=%llu)\n",
                s,
                static_cast<unsigned long long>(slot.offset_bytes),
                static_cast<unsigned long long>(slot.nbytes));
            std::abort();
          }
          assert(end_offset <= pool_bytes_ &&
                 "slot exceeds pool bounds");
          assert(slot.offset_bytes % ALIGNMENT == 0 &&
                 "slot offset not ALIGNMENT-aligned — sweep-line bug");
          ptr_table_[s] = base + slot.offset_bytes;
        }
      }
    }
  }

  [[gnu::cold]]
  void destroy() noexcept
      post (pool_       == nullptr)
      post (ptr_table_  == nullptr)
      post (pool_bytes_ == 0)
      post (num_slots_  == 0)
      post (num_external_ == 0)
  {
    if (pool_) crucible::rt::unregister_hot_region(pool_);
    std::free(pool_);
    std::free(ptr_table_);
    pool_         = nullptr;
    ptr_table_    = nullptr;
    pool_bytes_   = 0;
    num_slots_    = 0;
    num_external_ = 0;
  }

  // ── ScopedView-typed overloads ──────────────────────────────────────
  //
  // The supported API.  Callers obtain an InitializedView once per
  // init/destroy cycle via `mint_initialized_view()` — the view
  // construction fires the contract pre() that the pool is live.
  // Downstream calls thread the view through; the type system
  // guarantees slot_ptr / register_external / detach are reachable
  // only when the pool is initialized.
  using InitializedView =
      crucible::safety::ScopedView<PoolAllocator, pool_state::Initialized>;

  // Factory: mints an InitializedView for `*this`.  Contract fires if
  // the pool is not actually initialized, matching the runtime guard.
  [[nodiscard]] CRUCIBLE_INLINE InitializedView mint_initialized_view() const noexcept
      pre (is_initialized())
  {
    return crucible::safety::mint_view<pool_state::Initialized>(*this);
  }

  // Hot path — requires InitializedView proof.
  [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
  inline void* slot_ptr(SlotId sid, InitializedView const&) const noexcept
      CRUCIBLE_LIFETIMEBOUND
      pre (sid.raw() < num_slots_)
  {
    // Contract guarantees the bound; propagate to the optimizer so the
    // indexed load below compiles to a single MOV with no runtime check.
    [[assume(sid.raw() < num_slots_)]];
    return ptr_table_[sid.raw()];
  }

  // External registration — requires InitializedView proof.
  CRUCIBLE_INLINE void register_external(
      SlotId sid,
      crucible::safety::NonNull<void*> ptr,
      InitializedView const&) noexcept
      pre (sid.raw() < num_slots_)
  {
    ptr_table_[sid.raw()] = ptr.value();
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

  // ── ScopedView predicates (ADL-discovered by safety::mint_view) ──
  [[nodiscard]] friend constexpr bool view_ok(
      PoolAllocator const& p, std::type_identity<pool_state::Initialized>) noexcept {
    return p.is_initialized();
  }

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

  // Typed detach — requires an InitializedView to prove the pool is live.
  // After the call the view is semantically stale (the underlying pool is
  // empty); callers should not use it further.  The view's deleted op=
  // already prevents them from refreshing it by assignment.
  [[nodiscard, gnu::cold]]
  DetachedPool detach(InitializedView const&) noexcept {
    void* p          = pool_;
    uint64_t n       = pool_bytes_;
    crucible::rt::unregister_hot_region(p);
    pool_            = nullptr;
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

// Tier 2 opt-in: nothing inside PoolAllocator may be a ScopedView.
// ScopedViews must not outlive their construction scope, so storing
// one in a field would be an escape.  GCC 16 reflection walks the
// struct at compile time and fails this if any member (even nested
// through optional / vector / variant / etc.) is a ScopedView.
static_assert(crucible::safety::no_scoped_view_field_check<PoolAllocator>());

} // namespace crucible
