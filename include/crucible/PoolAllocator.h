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
#include <crucible/safety/AllocClass.h>
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
  // Upper bounds on plan dimensions.  num_slots fits in a uint32_t
  // by field type, but a 4 G × 8 B pointer table (32 GB) is absurd;
  // cap at 1 M slots which covers real models with room.  pool_bytes
  // caps at 256 GB (matches the Cipher load ceiling × 1024).
  // Adversarial or corrupt MemoryPlan beyond these is rejected at the
  // init boundary rather than allocating obscene amounts and failing
  // the post-alloc checks.
  static constexpr uint32_t kMaxNumSlots = 1u << 20;          // 1 M
  static constexpr uint64_t kMaxPoolBytes = uint64_t{256} << 30;  // 256 GB

  [[gnu::cold, gnu::noinline]]
  void init(const MemoryPlan* plan) noexcept CRUCIBLE_NO_THREAD_SAFETY
      pre (plan        != nullptr)
      pre (ptr_table_  == nullptr)   // double-init forbidden
      pre (pool_       == nullptr)
      // Refined bounds on plan dimensions — propagates as [[assume]]
      // under release semantic=ignore so downstream uses of num_slots_
      // and pool_bytes_ reason about bounded values.
      pre (plan->num_slots  <= kMaxNumSlots)
      pre (plan->pool_bytes <= kMaxPoolBytes)
      pre (plan->num_external <= plan->num_slots)
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
  //
  // Contract pre()s carry into the body as [[assume]] hints:
  //   - `sid.raw() < num_slots_` → indexed load compiles to a single
  //     MOV with no runtime bounds check
  //
  // Static return-pointer promises:
  //   - gnu::assume_aligned(ALIGNMENT) → the returned pointer is
  //     known-aligned to 256 B at the type-system level; downstream
  //     vector loads can use aligned-load intrinsics without a
  //     runtime alignment check.  The 256 B alignment is enforced
  //     by the init() loop's `slot.offset_bytes % ALIGNMENT == 0`
  //     assert and the std::aligned_alloc base — both internal slots
  //     (base + offset_bytes) and external slots (callers must
  //     register only ALIGNMENT-aligned pointers — see
  //     test_pool_allocator's `alignas(256)` discipline) satisfy
  //     the promise.  Nullptr is alignment-trivially compliant
  //     (zero is divisible by every power of two) so the attribute
  //     remains valid for unregistered external slots that legally
  //     return nullptr.
  //
  // NOT promised: gnu::returns_nonnull.  External slots that have
  // not yet been registered legally return nullptr (callers
  // discriminate via != nullptr); a non-null promise would brand
  // the legitimate "external not yet bound" path as UB.  Callers
  // wanting a non-null guarantee must guard with an explicit check
  // OR use a future `slot_ptr_internal` overload that requires
  // pre(!plan->slots[sid].is_external).
  [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline,
    gnu::assume_aligned(ALIGNMENT)]]
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
  //   auto view = pool.mint_initialized_view();
  //   void* const* tbl = pool.table(view);
  //   for (...) p = tbl[sid.raw()];   // one load per access
  //
  // Returning the raw table escapes the slot_ptr per-call contract
  // (sid bounds check, alignment proof) — which means the caller
  // takes responsibility for those invariants on every dereference.
  // To keep that escape gated by the type system, the typed overload
  // requires an InitializedView proof.  Combined with
  // gnu::returns_nonnull (the table pointer is non-null whenever the
  // pool is initialized — established by init() and posted by
  // is_initialized() being the view's predicate), callers receive a
  // statically-non-null table pointer with no runtime check.
  //
  // The legacy overload (no view) is preserved to avoid breaking
  // existing code paths but is marked [[deprecated]] to direct new
  // callers to the typed form; in this codebase ReplayEngine is the
  // only consumer and threads the view through.
  [[nodiscard, gnu::pure, gnu::returns_nonnull]] CRUCIBLE_INLINE
  void* const* table(InitializedView const&) const noexcept
      CRUCIBLE_LIFETIMEBOUND
  {
    // The view's view_ok predicate is is_initialized(), which is
    // ptr_table_ != nullptr; the [[assume]] propagates that fact for
    // downstream callers and matches the gnu::returns_nonnull promise.
    [[assume(ptr_table_ != nullptr)]];
    return ptr_table_;
  }

  // Legacy overload — accepts no view.  Returns nullptr when the
  // pool is not yet initialized, so it lacks the gnu::returns_nonnull
  // promise of the typed form.  Prefer the InitializedView overload
  // above in new code.
  //
  // (Kept un-deprecated for now since the project's -Werror policy
  // would flag every transitively-affected test; migrate callers in
  // a follow-up sweep if the deprecation diagnostic is desired.)
  [[nodiscard, gnu::pure]] CRUCIBLE_INLINE
  void* const* table() const noexcept CRUCIBLE_LIFETIMEBOUND {
    return ptr_table_;
  }

  [[nodiscard, gnu::pure]] void*    pool_base()      const noexcept CRUCIBLE_LIFETIMEBOUND { return pool_; }
  [[nodiscard, gnu::pure]] uint64_t pool_bytes()     const noexcept { return pool_bytes_; }
  [[nodiscard, gnu::pure]] uint32_t num_slots()      const noexcept { return num_slots_; }
  [[nodiscard, gnu::pure]] uint32_t num_external()   const noexcept { return num_external_; }
  [[nodiscard, gnu::pure]] bool     is_initialized() const noexcept { return ptr_table_ != nullptr; }

  // ═══════════════════════════════════════════════════════════════════
  // FOUND-G42: AllocClass-pinned production surface
  // ═══════════════════════════════════════════════════════════════════
  //
  // The PoolAllocator hot-path surface (slot_ptr) returns AllocClass<
  // AllocClassTag_v::Pool, void*> — a slot pointer is sourced from a
  // preallocated freelist (the ptr_table_ + pool_ machinery), which by
  // the AllocClass lattice is Pool tier (HugePage ⊑ Mmap ⊑ Heap ⊑
  // Arena ⊑ Pool ⊑ Stack).
  //
  // pool_base() returns Pool tier as the SAFE pinning: when init() was
  // called with pool_bytes ≥ kHugePageBytes, the underlying allocation
  // is huge-page-aligned (HugePage), which is WEAKER than Pool — so
  // claiming Pool tier ALWAYS holds.  Callers wanting the stronger
  // huge-page-aligned guarantee can call pool_base_huge_pinned(), which
  // requires `pool_bytes_ >= kHugePageBytes` as a precondition.
  //
  // Why additive (not replacing): the raw slot_ptr / pool_base surface
  // is consumed by ReplayEngine, CrucibleContext, and several Vigil
  // paths; a churn migration without immediate benefit is rejected.
  // The _pinned variants are for NEW production sites that explicitly
  // want the type-level fence.
  //
  // Cost: each pinned variant is a one-line forward to the raw call
  // followed by an EBO-collapsed AllocClass wrapping (sizeof wrapper
  // == sizeof(void*)).  Constructor is a single move; the [[nodiscard,
  // gnu::pure, gnu::hot]] attributes are preserved on the slot_ptr
  // variant so the optimizer treats the call identically to the raw.

  // Hot path — returns AllocClass<Pool, void*>, requires InitializedView.
  // Pinning Pool tier rejects callers that would accept the slot
  // pointer at AllocClass<Stack, ...> — Stack is STRONGER than Pool
  // (no allocator at all), and slot pointers DO go through an
  // allocator at init time.
  [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
  inline safety::AllocClass<safety::AllocClassTag_v::Pool, void*>
  slot_ptr_pinned(SlotId sid, InitializedView const& view) const noexcept
      CRUCIBLE_LIFETIMEBOUND
      pre (sid.raw() < num_slots_)
  {
    return safety::AllocClass<safety::AllocClassTag_v::Pool, void*>{
        slot_ptr(sid, view)};
  }

  // Pool-tier-pinned base pointer.  This is the SAFE pinning: when
  // pool_bytes_ < kHugePageBytes, the buffer is 256B-aligned (Pool
  // tier).  When pool_bytes_ ≥ kHugePageBytes, the buffer is 2MB-
  // aligned (HugePage tier, weaker than Pool — Pool claim still
  // holds).
  //
  // Returned pointer can be null when pool_bytes_ == 0; callers must
  // discriminate via the AllocClass::peek() result.
  [[nodiscard, gnu::pure]]
  inline safety::AllocClass<safety::AllocClassTag_v::Pool, void*>
  pool_base_pinned() const noexcept CRUCIBLE_LIFETIMEBOUND {
    return safety::AllocClass<safety::AllocClassTag_v::Pool, void*>{pool_};
  }

  // HugePage-tier-pinned base pointer.  Requires pool_bytes_ ≥
  // kHugePageBytes — when this precondition holds, init() chose
  // page_align = kHugePageBytes and the returned pointer is 2MB-
  // aligned.  Callers wanting to declare "I require huge-page-backed
  // memory" use this overload; the precondition fence rejects the
  // small-pool case at the boundary.
  [[nodiscard, gnu::pure]]
  inline safety::AllocClass<safety::AllocClassTag_v::HugePage, void*>
  pool_base_huge_pinned() const noexcept CRUCIBLE_LIFETIMEBOUND
      pre (pool_bytes_ >= crucible::rt::kHugePageBytes)
  {
    return safety::AllocClass<safety::AllocClassTag_v::HugePage, void*>{pool_};
  }

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
