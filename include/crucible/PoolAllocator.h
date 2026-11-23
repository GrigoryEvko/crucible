#pragma once

// Turns a memory plan into one aligned block of bytes plus a table that maps
// each slot to the address of its data and to its device. Slots owned
// elsewhere are registered by the owner and point outside the pool. The
// rest point into it.
//
// The block is a foundation::AlignedBytes from the zeroed mint, so each byte
// reads zero before its first write.  detach() gives it to the caller, so the
// caller can keep the old bytes alive while it builds the replacement. The
// table has three columns of one length: the addresses, the devices and the
// bytes. The replay reads only the address of a slot, so the column of
// addresses holds one word for each slot and a cache line holds eight of
// them. The device is read at the start of a plan or at a check, and one
// branded index reads the two columns with one bound check.
//
// Column of bytes
//   The bytes of each slot as a View: the window of the pool for a pooled
//   slot, the View that its owner registered for an external slot, or the
//   empty View for an external slot with no registered View.  The address of
//   a slot is the first byte of its View, or the View is empty: init builds
//   the address from the View, and each registration checks it.  A reader of
//   an input takes the bytes through
//   slot_view, which gives a read-only View of T.  No door of the pool gives
//   a mutable View of a slot: the context lends the outputs of an op through
//   the frame door with_mut_windows of the pool bytes.
//
// The block is memory of the host, so each pooled slot is on the host
// device, also when the plan records a different device for the slot. A
// registered slot keeps the device that its owner states.
//
// One thread builds it and the replay path only reads it once initialisation
// has returned.

#include <crucible/ForegroundCtx.h>
#include <crucible/MerkleDag.h>
#include <crucible/warden/Registry.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
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
    PoolAllocator(PoolAllocator&&) = delete("the entries of addresses_ are interior pointers into pool_");
    PoolAllocator& operator=(PoolAllocator&&) = delete("the entries of addresses_ are interior pointers into pool_");

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
    // that fails the same test, through the window of the pool and the
    // alignment, and a loader of a plan refuses one with it.
    [[nodiscard]] static constexpr bool can_serve_slot(const TensorSlot& slot, uint64_t pool_bytes) noexcept {
        if (slot.is_external) return true;
        const bool is_inside =
            ::foundation::core::checked_add(slot.offset_bytes, slot.nbytes)
                .is_some_and([pool_bytes](uint64_t end_offset) noexcept { return end_offset <= pool_bytes; });
        return is_inside && slot.offset_bytes % ALIGNMENT == 0;
    }

    // Failure to allocate ends the process. A runtime that preallocates
    // everything up front has nowhere to retreat to when the up-front
    // allocation fails. Externally owned slots start null and are registered
    // before replay.  The pool and the slot table allocate through the
    // allocation capability of the caller.
    //
    // init has no cold attribute.  GCC compiles a cold function for size, so
    // the doors of the loop of the slots stay out of line, and the loop of a
    // plan of 4096 slots took twice the cycles of each slot.
    [[gnu::noinline]]
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
            // Each byte of the pool reads zero until a slot writes it, in each
            // build, so a read before the first write of a slot gives the
            // same bytes on each run.  A large pool comes from fresh pages of
            // the kernel, so the mint writes no page outside ThreadSanitizer.
            ::fixy::Layout const layout = ::fixy::mint_layout(::fixy::ByteCount{plan.pool_bytes},
                                                              ::fixy::mint_alignment(::fixy::ByteCount{page_align})
                                                                  .expect("the alignment of a pool is a power of two"))
                                              .expect("the pool of a plan of at most kMaxPoolBytes is a layout");
            pool_ = ::foundation::mint_aligned_bytes_zeroed(allocation, layout);
            // The region holds the bytes of the pool, and not the bytes that
            // the round up to the alignment adds after them.
            crucible::warden::register_hot_region(::fixy::pages::storage_of(pool_.view()), is_huge);
        }

        if (plan_slots > 0) {
            ::fixy::HeapArray<::fixy::Option<::fixy::RunAddress>> address_column =
                ::fixy::mint_heap_array<::fixy::Option<::fixy::RunAddress>>(
                    allocation, plan_slots, ::fixy::Option<::fixy::RunAddress>{::fixy::none});
            devices_ = ::fixy::mint_heap_array<TensorDevice>(allocation, plan_slots, kHostDevice);
            ::fixy::HeapArray<::fixy::View<uint8_t>> view_column =
                ::fixy::mint_heap_array<::fixy::View<uint8_t>>(allocation, plan_slots, ::fixy::View<uint8_t>{});

            // Externally owned slots hold no address and no bytes. The rest
            // are the window of their bytes in the pool, and the address of
            // its first byte, on the host.
            //
            // The window of the pool is the check of the bounds of a slot: it
            // compares the offset with the count of the pool, and the count of
            // the slot with the bytes after the offset, so no sum can wrap.
            // The offsets come from shapes supplied by a foreign runtime, so
            // this stays a runtime check with a diagnostic rather than a
            // precondition.  The check stays armed in a release build.  The
            // window becomes a copy destination, so a slot reaching past the
            // pool would corrupt the heap silently instead of trapping.  A
            // slot off the alignment means the planner produced a slot that
            // the hot path then reads with aligned loads.  A contract clause
            // could not carry the check: it reads a slot of the plan this loop
            // walks, which no precondition on init can name.
            //
            // The two columns write through their frame doors, and the body of
            // each frame passes its lent run to write_pool_windows_.
            const ::fixy::View<uint8_t> pool = pool_.view();
            address_column = ::fixy::with_mut(std::move(address_column), [&](auto address_run) noexcept {
                view_column = ::fixy::with_mut(std::move(view_column), [&](auto view_run) noexcept {
                    write_pool_windows_(plan, pool, address_run.view(), view_run.view());
                });
            });
            addresses_ = std::move(address_column);
            views_ = std::move(view_column);
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
        addresses_ = ::fixy::HeapArray<::fixy::Option<::fixy::RunAddress>>{};
        devices_ = ::fixy::HeapArray<TensorDevice>{};
        views_ = ::fixy::HeapArray<::fixy::View<uint8_t>>{};
        num_external_ = 0;
        // Back to the default-constructed shape, which is what a subsequent
        // initialisation requires.
        CRUCIBLE_POST(0, pool_.size() == 0u);
        CRUCIBLE_POST(0, addresses_.size() == 0u);
        CRUCIBLE_POST(0, devices_.size() == 0u);
        CRUCIBLE_POST(0, views_.size() == 0u);
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

    // The address of the data of a slot, or no address for an external slot
    // that its owner did not register yet.  Callers test for that case.
    //
    // A pooled address is on the aligned base plus an offset that the
    // initialisation loop checks to be a multiple of the alignment, so it is
    // a multiple of ALIGNMENT.  An external one is registered by an owner
    // that aligns it.
    //
    // The read of the table refuses an id at or past the count of slots, so
    // an id from outside the plan ends the process.
    //
    // The pool owns its memory and its table, and a temporary pool releases
    // the two at the end of the statement.  A deleted `const&&` twin refuses
    // each read from a temporary pool, here and in the members that follow.
    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
    inline ::fixy::Option<::fixy::RunAddress> slot_ptr(SlotId sid, InitializedView const&) const& noexcept {
        return *addresses_.get(sid.raw()).expect("a slot id of the plan is below the count of its slots");
    }

    ::fixy::Option<::fixy::RunAddress> slot_ptr(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The address of the data of a slot and its device, or no address for an
    // external slot that its owner did not register yet.  One branded index
    // reads the two columns: the scope checks that the id is below the count
    // of slots, and that the two columns have one length, and each column
    // then reads with no check.  The device is not on the replay path, so
    // the second compare costs nothing there.
    [[nodiscard]] ::fixy::Option<TensorData> slot_data(SlotId sid, InitializedView const&) const& noexcept {
        return ::fixy::mint_branded_scope(addresses_.view(), [this, sid](auto const& addresses) noexcept {
            const auto devices =
                addresses.parallel(devices_.view()).expect("the two columns hold one entry for each slot");
            const auto index =
                addresses.index(sid.raw()).expect("a slot id of the plan is below the count of its slots");
            const TensorDevice device = devices.at(index);
            return addresses.at(index).match(
                [device](::fixy::RunAddress address) noexcept { return ::fixy::some(TensorData{address, device}); },
                [] noexcept { return ::fixy::Option<TensorData>{::fixy::none}; });
        });
    }

    ::fixy::Option<TensorData> slot_data(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The data of an external slot: its address and the device of its owner.
    // The two columns take the two values.  An address alone gives no bytes,
    // so the column of bytes takes the empty View, and slot_view gives no
    // value for the slot.
    CRUCIBLE_INLINE void register_external(SlotId sid, TensorData data, InitializedView const&) noexcept {
        write_slot_(sid, ::fixy::some(data.address()), data.device(), ::fixy::View<uint8_t>{});
        CRUCIBLE_POST(0, *addresses_.get(sid.raw()).expect("a slot id of the plan is below the count of its slots")
                             == ::fixy::some(data.address()));
    }

    // The bytes of an external slot as a View of the host.  The View borrows
    // its owner, and the owner outlives the activation of the region: the
    // pool keeps the View until the next init or destroy, and gives it to
    // each read of the slot.  The address of the slot is the first byte of
    // the View, or no address for a View of no byte.
    CRUCIBLE_INLINE void register_external_view(SlotId sid, ::fixy::View<uint8_t> bytes,
                                                InitializedView const&) noexcept {
        const ::fixy::Option<::fixy::RunAddress> address = ::fixy::mint_run_address(bytes, 0);
        write_slot_(sid, address, kHostDevice, bytes);
        CRUCIBLE_POST(0, is_address_of_bytes_(address, bytes));
    }

    // The bytes of a slot as a read-only View of T, for a reader of an input:
    // the window of the pool for a pooled slot, or the View that its owner
    // registered.  No value for the id with no slot, for an external slot
    // with no registered View, for a slot of no byte, and for bytes that hold
    // no whole count of T or whose first byte is not aligned for T.  The read
    // starts the lifetime of the elements of T over the bytes, with their
    // values.  Complexity: O(1).
    template <class T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] ::fixy::Option<::fixy::View<T const>> slot_view(SlotId sid, InitializedView const&) const& noexcept {
        using Found = ::fixy::Option<::fixy::View<T const>>;
        const auto found = views_.view().template window<1>(sid.raw());
        return found.match(
            [](::fixy::View<const ::fixy::View<uint8_t>, 1> entry) noexcept {
                const ::fixy::View<uint8_t const> bytes{entry.at(::fixy::index<0>())};
                const bool is_first_aligned = ::fixy::mint_run_address(entry.at(::fixy::index<0>()), 0)
                                                  .is_some_and([](::fixy::RunAddress const& first) noexcept {
                                                      return is_aligned<alignof(T)>(first);
                                                  });
                if (!is_first_aligned || bytes.size() % sizeof(T) != 0) return Found{::fixy::none};
                return Found::some(::foundation::lifetime::start_as_array<T>(bytes));
            },
            [] noexcept { return Found{::fixy::none}; });
    }

    template <class T>
    ::fixy::Option<::fixy::View<T const>> slot_view(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The column of addresses, for an inner loop that wants to hoist the
    // indirection out of itself.  The View holds the count of slots, so each
    // read of it checks its bound.  The view of the pool is required so that
    // the table comes only from a live pool.  The table is read once for each
    // activation of a region, not for each op.
    [[nodiscard, gnu::pure]] CRUCIBLE_INLINE ::fixy::View<const ::fixy::Option<::fixy::RunAddress>>
    table(InitializedView const&) const& noexcept {
        return addresses_.view();
    }

    ::fixy::View<const ::fixy::Option<::fixy::RunAddress>> table(InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The bytes of the pool, and the address of its first byte (no address
    // for a pool of no bytes).  The pool is memory of the host
    // (kHostDevice).  A const allocator lends writable memory here, as
    // slot_ptr does.
    [[nodiscard]] ::fixy::View<uint8_t> bytes() const& noexcept { return pool_.view(); }
    ::fixy::View<uint8_t> bytes() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");
    [[nodiscard, gnu::pure]] ::fixy::Option<::fixy::RunAddress> pool_base() const& noexcept { return pool_address_(); }
    ::fixy::Option<::fixy::RunAddress> pool_base() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");
    [[nodiscard, gnu::pure]] uint64_t pool_bytes() const noexcept { return pool_.size(); }
    // init bounds the count by kMaxNumSlots, so it fits 32 bits.
    [[nodiscard, gnu::pure]] uint32_t num_slots() const noexcept {
        return ::fixy::narrow<uint32_t>(addresses_.size()).expect("init bounds the count of slots by kMaxNumSlots");
    }
    [[nodiscard, gnu::pure]] uint32_t num_external() const noexcept { return num_external_; }
    [[nodiscard, gnu::pure]] constexpr bool is_initialized() const noexcept { return addresses_.size() > 0u; }

    // The same values, with their allocation tier carried in the return type.
    // A slot address comes out of a preallocated table, which is the pool
    // tier, and pinning it there rejects a caller that would take it as stack
    // memory, which is the stronger claim of having no allocator behind it.
    //
    // The pool tier is also the safe claim for the base address. When the
    // pool is large enough to be huge-page aligned its tier is weaker than
    // pool, so the pool claim holds either way. The huge-page variant below
    // states the stronger one and fences the small case out with a
    // precondition.
    using PinnedAddress = ::fixy::alloc_class::Pool<::fixy::Option<::fixy::RunAddress>>;
    using HugePinnedAddress = ::fixy::alloc_class::HugePage<::fixy::Option<::fixy::RunAddress>>;

    [[nodiscard, gnu::pure, gnu::hot, gnu::always_inline]]
    inline PinnedAddress slot_ptr_pinned(SlotId sid, InitializedView const& view) const& noexcept {
        return ::fixy::mint_band<PinnedAddress>(slot_ptr(sid, view));
    }

    PinnedAddress slot_ptr_pinned(SlotId, InitializedView const&) const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // No address when the pool has no bytes, so the caller inspects the
    // result.
    [[nodiscard, gnu::pure]]
    inline PinnedAddress pool_base_pinned() const& noexcept {
        return ::fixy::mint_band<PinnedAddress>(pool_address_());
    }

    PinnedAddress pool_base_pinned() const&& =
        delete("a temporary pool releases its memory at the end of the statement. Use a pool that has a name");

    // The precondition is what makes the stronger claim true: it is exactly
    // the condition under which initialisation chose the huge-page alignment.
    [[nodiscard, gnu::pure]]
    inline HugePinnedAddress pool_base_huge_pinned() const& noexcept {
        CRUCIBLE_PRE(pool_.size() >= crucible::warden::kHugePageBytes);
        return ::fixy::mint_band<HugePinnedAddress>(pool_address_());
    }

    HugePinnedAddress pool_base_huge_pinned() const&& =
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
    // `this` would skip.  The door gives the view on the erased identity
    // through erase_brand.  The brand of mint_view names the door and not the
    // caller, so each pool gets the same brand, and the rule brand-escape
    // refuses a fresh brand that leaves the door.  A brand for each pool
    // waits for the brand of an arena or a store.
    [[nodiscard]] CRUCIBLE_INLINE constexpr InitializedView initialized_view_() const noexcept {
        CRUCIBLE_PRE(is_initialized());
        return ::fixy::erase_brand(::fixy::mint_view<pool_state::Initialized>(*this));
    }

    // Writes the address and the bytes of each pooled slot of the plan into
    // the two lent runs of the columns.  One branded scope of the slots reads
    // the two runs at each index with no check, and the address comes from
    // the window, so the address of each pooled slot is the first byte of its
    // View by construction.  A pooled slot of no byte has no first byte, so
    // its address is the byte of the pool at its offset.  Complexity: one
    // pass over the slots.
    static void write_pool_windows_(const MemoryPlan& plan, ::fixy::View<uint8_t> pool,
                                    ::fixy::View<::fixy::Option<::fixy::RunAddress>> address_run,
                                    ::fixy::View<::fixy::View<uint8_t>> view_run) noexcept {
        static_cast<void>(::fixy::mint_branded_scope(plan.slots.view(), [&](auto const& slots) noexcept {
            auto const addresses =
                slots.parallel(address_run).expect("the column holds one entry for each slot of the plan");
            auto const views = slots.parallel(view_run).expect("the column holds one entry for each slot of the plan");
            for (auto const index : slots.indices()) {
                const TensorSlot& slot = slots.at(index);
                if (slot.is_external) continue;
                pool.window(slot.offset_bytes, slot.nbytes)
                    .match(
                        [&](::fixy::View<uint8_t> bytes) noexcept {
                            if (slot.offset_bytes % ALIGNMENT != 0) [[unlikely]]
                                refuse_slot_(slot, plan, index);
                            addresses.at(index) = bytes.size() == 0 ? ::fixy::mint_run_address(pool, slot.offset_bytes)
                                                                    : ::fixy::mint_run_address(bytes, 0);
                            views.at(index) = bytes;
                        },
                        [&]() noexcept { refuse_slot_(slot, plan, index); });
            }
            return ::fixy::Unit{};
        }));
    }

    // Ends the process for a pooled slot that the pool cannot serve: its
    // window ends past the pool, or its first byte is off the alignment.  The
    // path is cold and out of line, so the loop of init holds no object of
    // it, and each arm of the loop that refuses a slot names it alone.  The
    // position is the branded index of the slot in the scope of the loop.
    template <class Position>
    [[noreturn]] CRUCIBLE_COLD static void refuse_slot_(const TensorSlot& slot, const MemoryPlan& plan,
                                                        Position position) noexcept {
        ::fixy::fatal("PoolAllocator: slot {} does not fit the pool: offset={} nbytes={} pool_bytes={} alignment={}",
                      position.raw(), slot.offset_bytes, slot.nbytes, plan.pool_bytes, ALIGNMENT);
    }

    // The address of the first byte of the pool, or no address for a pool of
    // no bytes.
    [[nodiscard]] ::fixy::Option<::fixy::RunAddress> pool_address_() const noexcept {
        return ::fixy::mint_run_address(pool_.view(), 0);
    }

    // Writes the address, the device and the bytes of the slot sid into the
    // three columns, through the element door of each column.  An id at or
    // past the count of slots ends the process.  Complexity: O(1).
    CRUCIBLE_INLINE void write_slot_(SlotId sid, ::fixy::Option<::fixy::RunAddress> address, TensorDevice device,
                                     ::fixy::View<uint8_t> bytes) noexcept {
        addresses_.with_element(
            sid.raw(), [&address](::fixy::Option<::fixy::RunAddress>& slot) noexcept { slot = address; },
            [] noexcept { ::fixy::fatal("a slot id of the plan is below the count of its slots"); });
        devices_.with_element(
            sid.raw(), [device](TensorDevice& slot) noexcept { slot = device; },
            [] noexcept { ::fixy::fatal("the two columns hold one entry for each slot"); });
        views_.with_element(
            sid.raw(), [bytes](::fixy::View<uint8_t>& slot) noexcept { slot = bytes; },
            [] noexcept { ::fixy::fatal("the three columns hold one entry for each slot"); });
    }

    // True when the address is the first byte of the bytes, or the bytes are
    // empty.  A View of no byte has no first byte, so a pooled slot of no
    // byte and an external slot that its owner registered by its address
    // alone both agree (the column of bytes).
    [[nodiscard]] static constexpr bool is_address_of_bytes_(::fixy::Option<::fixy::RunAddress> address,
                                                             ::fixy::View<uint8_t> bytes) noexcept {
        return bytes.size() == 0 || address == ::fixy::mint_run_address(bytes, 0);
    }

    // True when the column holds the window of the pool of the pooled slot
    // sid of the plan: its address is the byte at the offset of the slot, and
    // its View holds the bytes of the slot.  A View that an owner registered
    // for a pooled slot fails it.  Complexity: O(1).
    [[nodiscard]] bool is_pool_window_(SlotId sid, TensorSlot const& slot) const noexcept {
        const ::fixy::View<uint8_t> held =
            *views_.get(sid.raw()).expect("the three columns hold one entry for each slot");
        return held.size() == slot.nbytes
            && *addresses_.get(sid.raw()).expect("the three columns hold one entry for each slot")
                   == ::fixy::mint_run_address(pool_.view(), slot.offset_bytes);
    }

    // The bytes of the pool.  A const allocator lends writable tensor
    // memory: slot_ptr, table and pool_base are const and give addresses that
    // the caller writes through.  The bytes are no part of the value of the
    // allocator, so the owner is mutable.
    mutable ::foundation::AlignedBytes pool_{};
    // The address of each slot, or no address for an external slot that its
    // owner did not register yet.  The replay reads this column.
    ::fixy::HeapArray<::fixy::Option<::fixy::RunAddress>> addresses_{};
    // The device of each slot, with the length of the column of addresses.
    // A pooled slot is on the host, and a registered slot is on the device of
    // its owner.
    ::fixy::HeapArray<TensorDevice> devices_{};
    // The bytes of each slot, with the length of the column of addresses
    // (the column of bytes).
    ::fixy::HeapArray<::fixy::View<uint8_t>> views_{};
    uint32_t num_external_ = 0;
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 4> pad_{};
};

}  // namespace crucible
