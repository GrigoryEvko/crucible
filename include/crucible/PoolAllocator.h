#pragma once

// Turns a memory plan into one aligned block of bytes plus a table that maps
// each slot to an address. Slots owned elsewhere are registered by the owner
// and point outside the pool. The rest point into it.
//
// The block is a foundation::AlignedBytes, and detach() gives it to the
// caller, so the caller can keep the old bytes alive while it builds the
// replacement. The table is a fixy::HeapArray of addresses.
//
// One thread builds it and the replay path only reads it once initialisation
// has returned.

#include <crucible/ForegroundCtx.h>
#include <crucible/MerkleDag.h>
#include <crucible/warden/Registry.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/os/Pages.h>
#include <foundation/AlignedBytes.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>
#include <utility>

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
    PoolAllocator(PoolAllocator&&) = delete("table_ entries are interior pointers into pool_");
    PoolAllocator& operator=(PoolAllocator&&) = delete("table_ entries are interior pointers into pool_");

    // The planner lays out slot offsets on this boundary, and it is what
    // coalesced device access and wide vector loads require.
    static constexpr uint32_t ALIGNMENT = 256;

    // Ceilings on what a plan may ask for. The slot count fits a 32-bit field,
    // but a pointer table for the whole range would be tens of gigabytes, and
    // a million slots covers a real model with room to spare. A plan past
    // either ceiling is refused here rather than after an absurd allocation.
    static constexpr uint32_t kMaxNumSlots = 1u << 20;
    static constexpr uint64_t kMaxPoolBytes = uint64_t{256} << 30;

    // True when a pool of pool_bytes can serve the slot.  An external slot
    // lives outside the pool.  A pooled slot must start on the alignment and
    // end inside the pool, with no wrap in the sum.  init aborts on a slot
    // that fails this, and a loader of a plan refuses one with it.
    [[nodiscard]] static constexpr bool can_serve_slot(const TensorSlot& slot, uint64_t pool_bytes) noexcept {
        if (slot.is_external) return true;
        const bool is_inside =
            ::foundation::core::checked_add(slot.offset_bytes, slot.nbytes)
                .match([pool_bytes](uint64_t end_offset) noexcept { return end_offset <= pool_bytes; },
                       [] noexcept { return false; });
        return is_inside && slot.offset_bytes % ALIGNMENT == 0;
    }

    // Failure to allocate ends the process. A runtime that preallocates
    // everything up front has nowhere to retreat to when the up-front
    // allocation fails. Externally owned slots start null and are registered
    // before replay.  The pool and the slot table allocate through the
    // allocation capability of the caller.
    [[gnu::cold, gnu::noinline]]
    void init(::foundation::effects::Alloc allocation, const MemoryPlan& plan) noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(!is_initialized());
        CRUCIBLE_PRE(pool_.size() == 0u);
        const size_t plan_slots = plan.slots.size();
        CRUCIBLE_PRE(::foundation::decide::in_range<size_t>(plan_slots, 0u, kMaxNumSlots));
        CRUCIBLE_PRE(::foundation::decide::in_range<uint64_t>(plan.pool_bytes, 0u, kMaxPoolBytes));
        CRUCIBLE_PRE(plan.num_external <= plan_slots);
        num_external_ = plan.num_external;

        if (plan.pool_bytes > 0) {
            // A pool at least a huge page long is aligned to one so it can be
            // backed by huge pages. Anything smaller takes the base alignment.
            const bool is_huge = plan.pool_bytes >= crucible::warden::kHugePageBytes;
            const uint64_t page_align = is_huge ? crucible::warden::kHugePageBytes : ALIGNMENT;
            pool_ = ::foundation::mint_aligned_bytes(allocation, plan.pool_bytes, page_align);
#ifndef NDEBUG
            // A read of unwritten pool memory stands out.
            ::fixy::fill(pool_.view(), uint8_t{0xCD});
#endif
            // The region holds the bytes of the pool, and not the bytes that
            // the round up to the alignment adds after them.
            crucible::warden::register_hot_region(::fixy::pages::storage_of(pool_.view()), is_huge);
        }

        if (plan_slots > 0) {
            table_ = ::fixy::mint_heap_array<void*>(allocation, plan_slots, nullptr);

            // Externally owned slots stay null. The rest are the address of
            // the byte at their offset.
            //
            // The sum is checked for wrap before it is compared. An offset
            // near the top of the range plus a modest size wraps to a small
            // number, which then compares as comfortably inside the pool while
            // the slot it describes runs off the end of it. The offsets come
            // from shapes supplied by a foreign runtime, so this stays a
            // runtime check with a diagnostic rather than a precondition.
            const ::fixy::View<uint8_t> pool = pool_.view();
            uint32_t slot_index = 0;
            for (const TensorSlot& slot : plan.slots) {
                // The check stays armed in a release build.  The next
                // statement publishes an address that later becomes a copy
                // destination, so a slot reaching past the pool would corrupt
                // the heap silently instead of trapping.  A slot off the
                // alignment means the planner produced a slot that the hot
                // path then reads with aligned loads.
                //
                // A contract clause could not carry the check: it reads a
                // slot of the plan this loop walks, which no precondition on
                // init can name.  This form is also independent of the
                // contract evaluation semantic, which differs across targets
                // in this tree.  Once per plan on a cold path.
                if (!can_serve_slot(slot, plan.pool_bytes)) [[unlikely]] {
                    ::fixy::fatal("PoolAllocator: slot {} does not fit the pool: offset={} nbytes={} pool_bytes={} "
                                  "alignment={}",
                                  slot_index, slot.offset_bytes, slot.nbytes, plan.pool_bytes, ALIGNMENT);
                }
                if (!slot.is_external) {
                    *table_.get(slot_index).expect("the table holds one entry for each slot of the plan") =
                        address_in_(pool, slot.offset_bytes);
                }
                ++slot_index;
            }
        }
        // A contract predicate that reads a member through `this` is skipped
        // when the compiler folds the body at compile time, so every such
        // check in this file runs from the body rather than a clause.
        CRUCIBLE_POST(0, num_slots() == plan_slots);
        CRUCIBLE_POST(0, pool_bytes() == plan.pool_bytes);
        CRUCIBLE_POST(0, ::foundation::decide::implies(plan_slots > 0u, is_initialized()));
    }

    // The destructor calls this on each path, so it has no cold attribute.
    // A cold callee there makes GCC compile each function that owns a pool
    // as an unlikely function.
    void destroy() noexcept {
        crucible::warden::unregister_hot_region(::fixy::pages::storage_of(pool_.view()));
        pool_.reset();
        table_ = ::fixy::HeapArray<void*>{};
        num_external_ = 0;
        // Back to the default-constructed shape, which is what a subsequent
        // initialisation requires.
        CRUCIBLE_POST(0, pool_.size() == 0u);
        CRUCIBLE_POST(0, table_.size() == 0u);
        CRUCIBLE_POST(0, num_external_ == 0u);
    }

    // A caller takes one of these once per initialise-and-destroy cycle and
    // threads it through. Minting it is where the liveness check happens, so
    // the methods that require it are unreachable on a dead pool.
    using InitializedView = ::fixy::ScopedView<PoolAllocator, pool_state::Initialized>;

    // The pool is foreground state, so the proof that it is live is minted
    // only on the thread that holds the producer claim of its Vigil.
    [[nodiscard]] CRUCIBLE_INLINE constexpr InitializedView mint_initialized_view(VigilFgCtx const&) const noexcept {
        CRUCIBLE_PRE(is_initialized());
        return initialized_view_();
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
    //
    // The read of the table refuses an id at or past the count of slots, so
    // an id from outside the plan ends the process.
    //
    // The pool owns its memory and its table, and a temporary pool releases
    // the two at the end of the statement.  A deleted `const&&` twin refuses
    // each read from a temporary pool, here and in the members that follow.
    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline, gnu::assume_aligned(ALIGNMENT)]]
    inline void* slot_ptr(SlotId sid, InitializedView const&) const& noexcept {
        return *table_.get(sid.raw()).expect("a slot id of the plan is below the count of its slots");
    }

    void* slot_ptr(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    CRUCIBLE_INLINE void register_external(SlotId sid, ::fixy::NonNull<void*> ptr, InitializedView const&) noexcept {
        auto entry = table_.get(sid.raw()).expect("a slot id of the plan is below the count of its slots");
        *entry = ptr.value();
        CRUCIBLE_POST(0, *entry == ptr.value());
    }

    // The raw table, for an inner loop that wants to hoist the indirection out
    // of itself. It escapes the per-call bounds and alignment guarantees, so
    // the caller owns those on every dereference from then on. The view is
    // required so that the escape is at least gated on the pool being live.
    [[nodiscard, gnu::pure, gnu::returns_nonnull]] CRUCIBLE_INLINE void* const*
    table(InitializedView const&) const& noexcept {
        // The view proves at least one slot, so the read of the first entry
        // does not fail.  The table is read once for each activation of a
        // region, not for each op.
        return &*table_.get(0).expect("an initialized pool holds at least one slot");
    }

    void* const* table(InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The bytes of the pool, and the address of its first byte (null for a
    // pool of no bytes).  A const allocator lends writable memory here, as
    // slot_ptr does.
    [[nodiscard]] ::fixy::View<uint8_t> bytes() const& noexcept { return pool_.view(); }
    ::fixy::View<uint8_t> bytes() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");
    [[nodiscard, gnu::pure]] void* pool_base() const& noexcept { return pool_address_(); }
    void* pool_base() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");
    [[nodiscard, gnu::pure]] uint64_t pool_bytes() const noexcept { return pool_.size(); }
    // init bounds the count by kMaxNumSlots, so it fits 32 bits.
    [[nodiscard, gnu::pure]] uint32_t num_slots() const noexcept {
        return ::fixy::narrow<uint32_t>(table_.size()).expect("init bounds the count of slots by kMaxNumSlots");
    }
    [[nodiscard, gnu::pure]] uint32_t num_external() const noexcept { return num_external_; }
    [[nodiscard, gnu::pure]] constexpr bool is_initialized() const noexcept { return table_.size() > 0u; }

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
    inline ::fixy::alloc_class::Pool<void*> slot_ptr_pinned(SlotId sid, InitializedView const& view) const& noexcept {
        return ::fixy::mint_band<::fixy::alloc_class::Pool<void*>>(slot_ptr(sid, view));
    }

    ::fixy::alloc_class::Pool<void*> slot_ptr_pinned(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // Null when the pool has no bytes, so the caller inspects the result.
    [[nodiscard, gnu::pure]]
    inline ::fixy::alloc_class::Pool<void*> pool_base_pinned() const& noexcept {
        return ::fixy::mint_band<::fixy::alloc_class::Pool<void*>>(pool_address_());
    }

    ::fixy::alloc_class::Pool<void*> pool_base_pinned() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The precondition is what makes the stronger claim true: it is exactly
    // the condition under which initialisation chose the huge-page alignment.
    [[nodiscard, gnu::pure]]
    inline ::fixy::alloc_class::HugePage<void*> pool_base_huge_pinned() const& noexcept {
        CRUCIBLE_PRE(pool_.size() >= crucible::warden::kHugePageBytes);
        return ::fixy::mint_band<::fixy::alloc_class::HugePage<void*>>(pool_address_());
    }

    ::fixy::alloc_class::HugePage<void*> pool_base_huge_pinned() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // Found by argument-dependent lookup from the view factory.
    [[nodiscard]] friend constexpr bool view_ok(PoolAllocator const& p,
                                                std::type_identity<pool_state::Initialized>) noexcept {
        return p.is_initialized();
    }

    // Hands the bytes out and empties this allocator, which lets a caller
    // keep the old data alive while it builds the replacement.  The view is
    // stale once this returns, since the pool it vouched for is now empty.
    // It cannot be refreshed by assignment either.
    [[nodiscard, gnu::cold]]
    ::foundation::AlignedBytes detach(InitializedView const&) noexcept {
        crucible::warden::unregister_hot_region(::fixy::pages::storage_of(pool_.view()));
        // The bytes move out before the reset, so the reset does not give
        // back a block whose ownership has already moved to the return value.
        ::foundation::AlignedBytes detached = std::move(pool_);
        destroy();
        return detached;
    }

private:
    // The engine and the context mint the view on paths that the gate of
    // their Vigil already passed, so they reach it without a context.
    friend struct ReplayEngine;
    friend struct CrucibleContext;

    // The precondition runs in the body, so it also fires when the compiler
    // folds a call at compile time, which a clause reading a member through
    // `this` would skip.
    [[nodiscard]] CRUCIBLE_INLINE constexpr InitializedView initialized_view_() const noexcept {
        CRUCIBLE_PRE(is_initialized());
        return ::fixy::mint_view<pool_state::Initialized>(*this);
    }

    // The address of the first byte of the pool, or null for a pool of no
    // bytes.
    [[nodiscard]] void* pool_address_() const noexcept { return address_in_(pool_.view(), 0); }

    // The address of the byte at offset in the pool.  A slot of zero bytes
    // can start at the end of the pool, and its address is then one past the
    // last byte.  A pool of no bytes gives null.
    [[nodiscard]] static void* address_in_(::fixy::View<uint8_t> pool, uint64_t offset) noexcept {
        if (pool.size() == 0) return nullptr;
        if (offset < pool.size()) return &*pool.get(offset).expect("the offset is below the size of the pool");
        return &*pool.get(pool.size() - 1).expect("the pool holds its last byte") + 1;
    }

    // The bytes of the pool.  A const allocator lends writable tensor
    // memory: slot_ptr, table and pool_base are const and give addresses that
    // the caller writes through.  The bytes are no part of the value of the
    // allocator, so the owner is mutable.
    mutable ::foundation::AlignedBytes pool_{};
    // The address of each slot, or null for an external slot that its owner
    // did not register yet.
    ::fixy::HeapArray<void*> table_{};
    uint32_t num_external_ = 0;
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 4> pad_{};
};

}  // namespace crucible
