#pragma once

// Turns a memory plan into one aligned allocation plus a table mapping each
// slot to a pointer. Slots owned elsewhere are registered by the owner and
// point outside the pool. The rest point into it.
//
// The buffer and the table are raw heap rather than owning wrappers, because
// detaching hands the buffer's ownership out to a separate sink. A wrapper
// that consumed itself on move would tie the buffer's lifetime to this object
// and make that impossible.
//
// One thread builds it and the replay path only reads it once initialisation
// has returned.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/warden/Registry.h>
#include <crucible/fixy/Struct.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace crucible {

// There is no tag for the empty state. The only method that requires it is
// initialisation, which is reached through the object itself.
namespace pool_state {
struct Initialized {};
}  // namespace pool_state

struct CRUCIBLE_OWNER PoolAllocator {
    PoolAllocator() = default;
    ~PoolAllocator() { destroy(); }

    PoolAllocator(const PoolAllocator&) = delete("interior pointers into pool_ would alias");
    PoolAllocator& operator=(const PoolAllocator&) = delete("interior pointers into pool_ would alias");
    PoolAllocator(PoolAllocator&&) = delete("ptr_table_ entries are interior pointers into pool_");
    PoolAllocator& operator=(PoolAllocator&&) = delete("ptr_table_ entries are interior pointers into pool_");

    // The planner lays out slot offsets on this boundary, and it is what
    // coalesced device access and wide vector loads require.
    static constexpr uint32_t ALIGNMENT = 256;

    // Owns a detached buffer and frees it. It is neither copyable nor movable
    // and is still returned by value: guaranteed elision covers that, and the
    // class-level nodiscard forces the caller to bind it to a named local
    // rather than let a temporary free the buffer at the end of the statement.
    struct [[nodiscard]] CRUCIBLE_OWNER DetachedPool {
        void* base = nullptr;
        uint64_t bytes = 0;

        DetachedPool() = default;
        DetachedPool(void* b, uint64_t n) noexcept : base{b}, bytes{n} {}
        ~DetachedPool() { std::free(base); }

        DetachedPool(const DetachedPool&) = delete("would double-free the pool buffer");
        DetachedPool& operator=(const DetachedPool&) = delete("would double-free the pool buffer");
        DetachedPool(DetachedPool&&) = delete("consumed at detach site via guaranteed elision");
        DetachedPool& operator=(DetachedPool&&) = delete("consumed at detach site via guaranteed elision");
    };

    // Ceilings on what a plan may ask for. The slot count fits a 32-bit field,
    // but a pointer table for the whole range would be tens of gigabytes, and
    // a million slots covers a real model with room to spare. A plan past
    // either ceiling is refused here rather than after an absurd allocation.
    static constexpr uint32_t kMaxNumSlots = 1u << 20;
    static constexpr uint64_t kMaxPoolBytes = uint64_t{256} << 30;

    // Failure to allocate aborts. A runtime that preallocates everything up
    // front has nowhere to retreat to when the up-front allocation fails.
    // Externally owned slots start null and are registered before replay.
    [[gnu::cold, gnu::noinline]]
    void init(const MemoryPlan* plan) noexcept CRUCIBLE_NO_THREAD_SAFETY pre(plan != nullptr) pre(ptr_table_ == nullptr)
        pre(pool_ == nullptr) pre(::crucible::decide::in_range<uint32_t>(plan->num_slots, 0u, kMaxNumSlots))
            pre(::crucible::decide::in_range<uint64_t>(plan->pool_bytes, 0u, kMaxPoolBytes))
                pre(plan->num_external <= plan->num_slots) {
        num_slots_ = plan->num_slots;
        num_external_ = plan->num_external;
        pool_bytes_ = plan->pool_bytes;

        if (pool_bytes_ > 0) {
            // A pool at least a huge page long is aligned to one so it can be
            // backed by huge pages. Anything smaller takes the base alignment.
            const size_t page_align =
                (pool_bytes_ >= crucible::warden::kHugePageBytes) ? crucible::warden::kHugePageBytes : ALIGNMENT;
            // The preconditions bound both operands far below the point where
            // the sum could wrap, so the saturating form never saturates here.
            // It is there so that raising either ceiling later cannot turn a
            // rounding-up into a wrap.
            const uint64_t padded = ::crucible::fixy::struct_::saturating_add<uint64_t>(pool_bytes_, page_align - 1);
            const uint64_t alloc_size = padded & ~(page_align - 1);
            pool_ = std::aligned_alloc(page_align, alloc_size);
            if (!pool_) [[unlikely]]
                std::abort();
#ifndef NDEBUG
            std::memset(pool_, 0xCD, alloc_size);  // a read of unwritten pool memory stands out
#endif
            const bool huge = (page_align == crucible::warden::kHugePageBytes);
            crucible::warden::register_hot_region(pool_, alloc_size,
                                                  /*huge=*/huge, "PoolAllocator.pool");
        }

        if (num_slots_ > 0) {
            ptr_table_ = static_cast<void**>(std::calloc(num_slots_, sizeof(void*)));
            if (!ptr_table_) [[unlikely]]
                std::abort();

            // Externally owned slots stay null. The rest are the base plus
            // their offset.
            //
            // The sum is checked for wrap before it is compared. An offset
            // near the top of the range plus a modest size wraps to a small
            // number, which then compares as comfortably inside the pool while
            // the slot it describes runs off the end of it. The offsets come
            // from shapes supplied by a foreign runtime, so this stays a
            // runtime check with a diagnostic rather than a precondition.
            auto* base = static_cast<char*>(pool_);
            for (uint32_t s = 0; s < num_slots_; ++s) {
                const auto& slot = plan->slots[s];
                if (!slot.is_external) {
                    uint64_t end_offset;
                    if (__builtin_add_overflow(slot.offset_bytes, slot.nbytes, &end_offset)) [[unlikely]] {
                        std::fprintf(stderr,
                                     "PoolAllocator: slot %u offset_bytes+nbytes overflow "
                                     "(offset=%llu nbytes=%llu)\n",
                                     s, static_cast<unsigned long long>(slot.offset_bytes),
                                     static_cast<unsigned long long>(slot.nbytes));
                        std::abort();
                    }
                    // Both checks stay armed in a release build. The next
                    // statement publishes a pointer that later becomes a
                    // memcpy destination, so a slot reaching past the pool
                    // corrupts the heap silently instead of trapping.
                    //
                    // A contract clause could not carry either one anyway:
                    // they read a slot of a plan this loop is walking, which
                    // no precondition on init can name. This form is also
                    // independent of the contract evaluation semantic, which
                    // is a per-target build option that already differs
                    // across targets in this tree. Once per plan on a cold
                    // path, so the cost is nil.
                    CRUCIBLE_FATAL_INVARIANT(end_offset <= pool_bytes_);
                    // An offset off the alignment means the sweep-line
                    // planner produced a slot the hot path then reads with
                    // aligned loads.
                    CRUCIBLE_FATAL_INVARIANT(slot.offset_bytes % ALIGNMENT == 0);
                    ptr_table_[s] = base + slot.offset_bytes;
                }
            }
        }
        // A contract predicate that reads a member through `this` is skipped
        // when the compiler folds the body at compile time, so every such
        // check in this file runs from the body rather than a clause.
        CRUCIBLE_POST(0, num_slots_ == plan->num_slots);
        CRUCIBLE_POST(0, pool_bytes_ == plan->pool_bytes);
        CRUCIBLE_POST(0, ::crucible::decide::implies(pool_bytes_ > 0u, pool_ != nullptr));
        CRUCIBLE_POST(0, ::crucible::decide::implies(num_slots_ > 0u, ptr_table_ != nullptr));
    }

    [[gnu::cold]]
    void destroy() noexcept {
        if (pool_) crucible::warden::unregister_hot_region(pool_);
        std::free(pool_);
        std::free(ptr_table_);
        pool_ = nullptr;
        ptr_table_ = nullptr;
        pool_bytes_ = 0;
        num_slots_ = 0;
        num_external_ = 0;
        // Back to the default-constructed shape, which is what a subsequent
        // initialisation requires.
        CRUCIBLE_POST(0, pool_ == nullptr);
        CRUCIBLE_POST(0, ptr_table_ == nullptr);
        CRUCIBLE_POST(0, pool_bytes_ == 0u);
        CRUCIBLE_POST(0, num_slots_ == 0u);
        CRUCIBLE_POST(0, num_external_ == 0u);
    }

    // A caller takes one of these once per initialise-and-destroy cycle and
    // threads it through. Minting it is where the liveness check happens, so
    // the methods that require it are unreachable on a dead pool.
    using InitializedView = crucible::fixy::wrap::ScopedView<PoolAllocator, pool_state::Initialized>;

    [[nodiscard]] CRUCIBLE_INLINE InitializedView mint_initialized_view() const noexcept pre(is_initialized()) {
        return crucible::fixy::wrap::mint_view<pool_state::Initialized>(*this);
    }

    // The alignment promise holds on both kinds of slot: an internal one is
    // the aligned base plus an offset the initialisation loop asserts is a
    // multiple of the alignment, and an external one is registered by an owner
    // that aligns it. A null pointer satisfies any alignment, so the promise
    // survives an external slot that has not been registered yet.
    //
    // There is deliberately no non-null promise. An external slot that has not
    // been registered returns null and callers test for it, so promising
    // non-null would make that legitimate path undefined.
    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline, gnu::assume_aligned(ALIGNMENT)]]
    inline void* slot_ptr(SlotId sid, InitializedView const&) const noexcept CRUCIBLE_LIFETIMEBOUND {
        // The count guard is not redundant with the range check: at a count of
        // zero the upper bound below underflows to the largest value and the
        // range admits everything. The view already proves the count is
        // positive, so this catches a later change that opens the path.
        CRUCIBLE_PRE(num_slots_ > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(sid.raw(), 0u, num_slots_ - 1u));
        return ptr_table_[sid.raw()];
    }

    CRUCIBLE_INLINE void register_external(SlotId sid, crucible::fixy::wrap::NonNull<void*> ptr,
                                           InitializedView const&) noexcept {
        CRUCIBLE_PRE(num_slots_ > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(sid.raw(), 0u, num_slots_ - 1u));
        ptr_table_[sid.raw()] = ptr.value();
        CRUCIBLE_POST(0, ptr_table_[sid.raw()] == ptr.value());
    }

    // The raw table, for an inner loop that wants to hoist the indirection out
    // of itself. It escapes the per-call bounds and alignment guarantees, so
    // the caller owns those on every dereference from then on. The view is
    // required so that the escape is at least gated on the pool being live.
    [[nodiscard, gnu::pure, gnu::returns_nonnull]] CRUCIBLE_INLINE void* const*
    table(InitializedView const&) const noexcept CRUCIBLE_LIFETIMEBOUND {
        // What the view already proves, restated for the optimiser so it
        // matches the non-null promise on the return type.
        [[assume(ptr_table_ != nullptr)]];
        return ptr_table_;
    }

    [[nodiscard, gnu::pure]] void* pool_base() const noexcept CRUCIBLE_LIFETIMEBOUND { return pool_; }
    [[nodiscard, gnu::pure]] uint64_t pool_bytes() const noexcept { return pool_bytes_; }
    [[nodiscard, gnu::pure]] uint32_t num_slots() const noexcept { return num_slots_; }
    [[nodiscard, gnu::pure]] uint32_t num_external() const noexcept { return num_external_; }
    [[nodiscard, gnu::pure]] bool is_initialized() const noexcept { return ptr_table_ != nullptr; }

    // The same values, with their allocation tier carried in the return type.
    // A slot pointer comes out of a preallocated table, which is the pool
    // tier, and pinning it there rejects a caller that would take it as stack
    // memory, which is the stronger claim of having no allocator behind it.
    //
    // The pool tier is also the safe claim for the base pointer. When the pool
    // is large enough to be huge-page aligned its tier is weaker than pool, so
    // the pool claim holds either way. The huge-page variant below states the
    // stronger one and fences the small case out with a precondition.
    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
    inline ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Pool, void*>
    slot_ptr_pinned(SlotId sid, InitializedView const& view) const noexcept CRUCIBLE_LIFETIMEBOUND {
        return ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Pool, void*>{
            slot_ptr(sid, view)};
    }

    // Null when the pool has no bytes, so the caller inspects the result.
    [[nodiscard, gnu::pure]]
    inline ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Pool, void*>
    pool_base_pinned() const noexcept CRUCIBLE_LIFETIMEBOUND {
        return ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Pool, void*>{pool_};
    }

    // The precondition is what makes the stronger claim true: it is exactly
    // the condition under which initialisation chose the huge-page alignment.
    [[nodiscard, gnu::pure]]
    inline ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::HugePage, void*>
    pool_base_huge_pinned() const noexcept CRUCIBLE_LIFETIMEBOUND pre(pool_bytes_ >= crucible::warden::kHugePageBytes) {
        return ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::HugePage, void*>{pool_};
    }

    // Found by argument-dependent lookup from the view factory.
    [[nodiscard]] friend constexpr bool view_ok(PoolAllocator const& p,
                                                std::type_identity<pool_state::Initialized>) noexcept {
        return p.is_initialized();
    }

    // Hands the buffer out and empties this allocator, which lets a caller
    // keep the old data alive while it builds the replacement.
    [[nodiscard, gnu::cold]]
    DetachedPool detach() noexcept pre(pool_ != nullptr) {
        void* p = pool_;
        uint64_t n = pool_bytes_;
        crucible::warden::unregister_hot_region(p);
        // Cleared before the reset so that the reset does not free a buffer
        // whose ownership has already moved to the return value.
        pool_ = nullptr;
        destroy();
        return DetachedPool{p, n};
    }

    // The view is stale once this returns, since the pool it vouched for is
    // now empty. It cannot be refreshed by assignment either.
    [[nodiscard, gnu::cold]]
    DetachedPool detach(InitializedView const&) noexcept {
        void* p = pool_;
        uint64_t n = pool_bytes_;
        crucible::warden::unregister_hot_region(p);
        pool_ = nullptr;
        destroy();
        return DetachedPool{p, n};
    }

private:
    void* pool_ = nullptr;
    void** ptr_table_ = nullptr;
    uint64_t pool_bytes_ = 0;
    uint32_t num_slots_ = 0;
    uint32_t num_external_ = 0;
};

static_assert(sizeof(PoolAllocator) == 32, "PoolAllocator layout: 2 ptrs + u64 + 2*u32");
static_assert(alignof(PoolAllocator) == 8);

// A view must not outlive the scope it was minted in, so storing one in a
// field would let it escape. This walks the struct, nested members included.
static_assert(crucible::fixy::wrap::no_scoped_view_field_check<PoolAllocator>());

}  // namespace crucible
