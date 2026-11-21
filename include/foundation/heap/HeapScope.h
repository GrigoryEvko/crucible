#pragma once

// The typed testing door of the allocator of the base: a scope of the heap of
// the calling thread, and the blocks of a constant class that it gives (THE
// SCOPE).
//
//   heap_tag::Owner                  the tag of the permission of the owner
//                                    thread of a heap
//   SlabSizeClass<Class>             a class with a slab, 1 to 52: the
//                                    refinement of the class of each door
//   mint_heap_scope(capability, body)
//                                    calls body with the HeapScope of the heap
//                                    of the calling thread, and gives the
//                                    result of body
//   HeapScope<Brand>                 the scope: a Ref to the HeapState of the
//                                    thread and the permission of its owner
//   scope.allocate<Class>()          a LocalBlock<Class, Brand> from the bin
//                                    of the class
//   scope.release(std::move(block))  the block back into the bin of its class
//   scope.census(), scope.flush_bins()
//                                    the testing doors of the heap
//   LocalBlock<Class, Brand>         one block of the class from the heap of
//                                    the scope: linear and not sendable
//
// THE SCOPE
//   mint_heap_scope is a scope mint: its last template parameter is the scope
//   brand CRUCIBLE_SCOPE_BRAND of foundation/Brand.h, fresh for each call site.
//   The scope, the permission of the owner that it holds, and each block that
//   it gives carry that brand.  The door of the tag heap_tag::Owner mints the
//   permission on the brand with mint_scope_permission of
//   foundation/permissions/Permission.h (THE SCOPE MINT there).  So:
//     - The release of a scope of another brand does not take a block
//       (neg_heap_scope_release_into_other_brand).
//     - The concept of the mint refuses a result of the body that names the
//       brand (neg_heap_scope_result_names_brand), and the rule brand-escape
//       refuses a result that holds it in a capture
//       (neg_heap_scope_block_in_returned_closure).
//     - The rule brand-escape refuses each object of static or thread storage
//       duration that holds the brand (neg_heap_scope_block_in_static).
//   The release of a block thus reads no owner entry: the brand and the marks
//   not_sendable put the block and the scope on one thread and in the call
//   of one mint, and on one thread a scope names the one heap of the thread.
//   A scope cannot copy or move.  The end of the body ends the scope, and the
//   end of the scope ends the permission.
//
//   The mint reads the cell of the thread, and it claims the heap only for a
//   thread on the sentinel, on a cold branch, so the scope never holds the
//   sentinel.  A call of the cold claim on each path would make GCC compile
//   the mint and each body as cold code.  Each door takes only a class with a
//   slab, by a concept on its template argument
//   (neg_heap_scope_class_without_slab); the large runs extend it.  The scope
//   handle of the allocator extends this door on the same handle: make<T>, a
//   LocalBox, send and adopt.
//
//   A block is a number that no path reads through.  address() gives it to a
//   test, which reads the offsets of the layout.  An assignment of a block
//   over a live block drops the target with no release, as an assignment of
//   a Permission does, and the linear rules of the plugin read it.
//
// This header is in the layer F1_rest of utils/rule-table.txt, above
// foundation/permissions.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace foundation::heap {

namespace detail {

// The one builder of a HeapScope and the scope door of heap_tag::Owner: the
// mint below calls it.
struct HeapScopeDoor;

}  // namespace detail

namespace heap_tag {

// The tag of the permission of the owner thread of a heap.  The permission
// names no effect, so its row is empty, and a mint takes no context.  The one
// door that mints the permission on a scope brand is HeapScopeDoor.
struct Owner final {
    using permission_row = ::foundation::effects::Row<>;
    using scope_door = detail::HeapScopeDoor;
};

}  // namespace heap_tag

// A class with a slab, 1 to 52 (THE SCOPE).
template <::foundation::alloc::SizeClass Class>
concept SlabSizeClass = ::foundation::alloc::is_slab_class(Class);

template <::foundation::brand::IsBrand Brand>
class HeapScope;

// One block of a class with a slab from the heap of the scope of Brand (THE
// SCOPE).  Only a scope makes one, and only the release of a scope of the
// same brand takes it.
template <::foundation::alloc::SizeClass Class, ::foundation::brand::IsBrand Brand>
    requires SlabSizeClass<Class>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::transfer::not_sendable{}]]
                   [[= ::foundation::transfer::not_shareable{}]] LocalBlock final {
public:
    using brand_type = Brand;

    LocalBlock(LocalBlock const&) = delete("a block has one owner: two owners would release it two times");
    LocalBlock& operator=(LocalBlock const&) = delete("a block has one owner");
    constexpr LocalBlock(LocalBlock&&) noexcept = default;
    constexpr LocalBlock& operator=(LocalBlock&&) noexcept = default;
    ~LocalBlock() = default;

    // The first byte of the block, as a number that no path reads through.
    [[nodiscard]] constexpr ::foundation::alloc::AddressWord address() const noexcept { return address_; }

private:
    friend class HeapScope<Brand>;

    constexpr explicit LocalBlock(::foundation::alloc::AddressWord address) noexcept : address_{address} {}

    ::foundation::alloc::AddressWord address_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The scope of the heap of the calling thread (THE SCOPE).  The scope cannot
// move, and its destructor is the door that ends the permission it holds.
template <::foundation::brand::IsBrand Brand>
class[[= ::foundation::transfer::not_sendable{}]][[= ::foundation::transfer::not_shareable{}]]
     [[= ::foundation::linear::scope_end_door{}]] HeapScope final {
public:
    using brand_type = Brand;

    HeapScope(HeapScope const&) = delete("a scope names the heap of its thread for the body of its mint");
    HeapScope& operator=(HeapScope const&) = delete("a scope names one heap for the body of its mint");
    HeapScope(HeapScope&&) = delete("a scope lives in the frame of its mint, and the body takes it by reference");
    HeapScope& operator=(HeapScope&&) = delete("a scope names one heap for the body of its mint");
    ~HeapScope() = default;

    // One block of the class from the bin of the class.  Complexity: O(1),
    // the six instructions of the fast path of the allocation of
    // foundation/alloc/HeapState.h.
    template <::foundation::alloc::SizeClass Class>
        requires SlabSizeClass<Class>
    [[nodiscard, gnu::always_inline]] LocalBlock<Class, Brand> allocate() noexcept {
        return LocalBlock<Class, Brand>{heap_->allocate(Class)};
    }

    // The block back into the bin of its class, with no read of its owner
    // entry.  Complexity: O(1), the six instructions of the fast path of the
    // release.
    template <::foundation::alloc::SizeClass Class>
    [[gnu::always_inline]] void release(LocalBlock<Class, Brand>&& block) noexcept {
        heap_->release(Class, block.address_);
        ::foundation::linear::drop(std::move(block));
    }

    // A named block stays with the caller, so a release of it would leave a
    // second name for a released block.
    template <::foundation::alloc::SizeClass Class>
    void release(LocalBlock<Class, Brand>&) = delete("the release takes the block that the caller gives up: pass it "
                                                     "with std::move");

    // The counts of the heap of the scope (THE CENSUS of
    // foundation/alloc/HeapState.h).
    [[nodiscard]] ::foundation::alloc::HeapCensus census() const noexcept { return heap_->census(); }

    // Gives each block of each bin of the heap back to its slab.
    void flush_bins() noexcept { heap_->flush_bins(); }

private:
    friend struct detail::HeapScopeDoor;

    HeapScope(::foundation::alloc::HeapState& heap,
              ::foundation::permissions::Permission<heap_tag::Owner, Brand>&& owner) noexcept
        : heap_{::foundation::core::detail::RefDoor::lend<Brand>(heap)}, owner_{std::move(owner)} {}

    ::foundation::core::Ref<::foundation::alloc::HeapState, Brand> heap_;
    [[no_unique_address]] ::foundation::permissions::Permission<heap_tag::Owner, Brand> owner_;
};

namespace detail {

// The scope door of heap_tag::Owner (THE SCOPE MINT of
// foundation/permissions/Permission.h): it builds the key of the scope mint,
// and the one caller gives it the scope brand of its own call.
struct HeapScopeDoor {
    template <::foundation::brand::IsBrand Brand>
    [[nodiscard]] static HeapScope<Brand> open(::foundation::alloc::HeapState& heap) noexcept {
        ::foundation::permissions::scope_mint_key<heap_tag::Owner> const key{};
        return HeapScope<Brand>{heap, ::foundation::permissions::mint_scope_permission<heap_tag::Owner, Brand>(key)};
    }
};

}  // namespace detail

// The arguments of mint_heap_scope: a body that takes the scope by reference
// with no throw, and gives a value that does not name the brand (THE SCOPE).
template <class Brand, class Body>
concept CanMintHeapScope =
    ::foundation::brand::IsBrand<Brand> && std::is_nothrow_invocable_v<Body&&, HeapScope<Brand>&>
    && !std::is_void_v<std::invoke_result_t<Body&&, HeapScope<Brand>&>>
    && !::foundation::core::detail::names_brand(^^std::invoke_result_t<Body&&, HeapScope<Brand>&>, ^^Brand);

// Calls body with the scope of the heap of the calling thread, which holds
// the permission of its owner on the scope brand of this call site, and gives
// the result of body.  A thread on the sentinel claims its heap first.  The
// capability is the authority to allocate, and the scope holds no copy of it.
// The pack refuses each explicit argument, so no caller names the brand.
template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0 && CanMintHeapScope<Fresh, Body>)
[[nodiscard]] auto mint_heap_scope(::foundation::core::Alloc const& capability, Body&& body) noexcept {
    static_cast<void>(capability);
    HeapScope<Fresh> scope = detail::HeapScopeDoor::open<Fresh>(::foundation::alloc::thread_heap());
    return static_cast<Body&&>(body)(scope);
}

}  // namespace foundation::heap
