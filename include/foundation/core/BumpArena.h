#pragma once

// The arena of the Ref family: a bump allocator over a list of blocks, and
// the epoch that ends its bytes.
//
//   BumpArena(block_size)       an arena that holds no block.  The first
//                               allocation takes the first block
//   make<T>(alloc, args...)     one T that the arguments build in the arena,
//                               as a Ref with the fresh brand of the call
//   array(alloc, count, fill)   count copies of fill in the arena, as a View.
//                               A count of zero gives the empty View and takes
//                               no bytes
//   with_epoch(body)            calls body, and then ends each byte that the
//                               arena gave out during the call
//   retained_bytes()            the bytes of each block that the arena holds
//
// An allocation is a bump of an offset in the current block and a mask of
// the address for the alignment.  The allocation capability is the first
// argument, so each allocation shows at its call site.  When the block is
// full, the cold path moves to the next block that holds the request.  Block
// k holds at least block_size << k bytes (saturating), so 64 blocks hold more
// bytes than an address space has, and the list is a fixed array with no
// heap of its own.  Each block comes from the allocation door of
// foundation/core/Heap.h.
//
// The arena ends no object, so make and array take only a type with a
// trivial destructor.  A Ref or a View of the arena owns nothing, and it
// dangles when the arena ends or when its epoch ends.  Its bytes die all
// together, in one of two ways:
//   - The arena ends.  Its destructor gives each block back to the door.
//   - An epoch ends.  with_epoch(body) opens an epoch on an empty arena, so
//     the epoch covers each byte that the arena gives out during the body.
//     After the body, each of those bytes dies, and the arena keeps its
//     blocks for the next epoch.  So a loop that runs an epoch in each turn
//     takes no block in its steady state.  An arena that holds more than
//     retained_bytes_limit bytes at the end gives back each block but the
//     first.  An epoch in a different epoch of one arena, and an epoch of an
//     arena that holds bytes, end the process.
//
// A View has no brand, so no type refuses a View that leaves the body of an
// epoch.  The check is at run time: in a build with AddressSanitizer, the end
// of an epoch poisons each block, and each allocation unpoisons only the
// bytes that it gives (foundation/os/Poison.h).  A read through a View of an
// ended epoch then ends the process with a report.
//
// The cold path, the end of an epoch and the end of the arena are in
// src/foundation/os/BumpArena.cpp, which calls the allocation door and the
// poison door.  The poison door is in a layer above this header, so this
// header declares the one call that the bump needs.

#include <foundation/Brand.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace foundation::core {

// The arguments of make<T>: a type that the arena can hold and that the
// arguments build with no throw, and the allocation capability.
template <class T, class Marker, class... Args>
concept CanMakeInArena = RefTarget<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>
                      && std::is_nothrow_constructible_v<T, Args&&...> && AllocationCapability<Marker>;

// The arguments of array: an element that the arena can hold and that copies
// with no throw, and the allocation capability.
template <class T, class Marker>
concept CanFillArenaArray = ViewElement<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>
                         && std::is_nothrow_copy_constructible_v<T> && AllocationCapability<Marker>;

// The body of an epoch: a call with no argument and no throw.
template <class Body>
concept EpochBody = std::is_nothrow_invocable_v<Body&&>;

namespace detail {

// Marks the count bytes from first as addressable, for the bump.  Only a
// build with AddressSanitizer has a body, in src/foundation/os/BumpArena.cpp.
// In each other build the call costs nothing.
#if defined(__SANITIZE_ADDRESS__)
void bump_unpoison_(void const* first, std::size_t count) noexcept;
#else
inline void bump_unpoison_(void const* /*first*/, std::size_t /*count*/) noexcept {}
#endif

// Ends the process for an array of count elements of element_bytes bytes
// past the largest count of a View.  The report is out of line, so the frame
// of array() holds no arguments of a report and no stack canary.
[[noreturn, gnu::cold, gnu::noinline]] void refuse_arena_array_(std::size_t count, std::size_t element_bytes) noexcept;

}  // namespace detail

class BumpArena final {
public:
    // The bytes of blocks that an arena keeps when an epoch ends.  An arena
    // that holds more gives back each block but the first, so one epoch with
    // an unusual peak does not pin its peak for the life of the arena.
    static constexpr std::size_t retained_bytes_limit = std::size_t{64} << 20;

    // The count of blocks that one arena can hold.
    static constexpr std::uint32_t max_blocks = 64;

    // The alignment of each block.  A larger alignment of an element costs
    // slack bytes in the block, because the bump aligns the address.
    static constexpr std::size_t block_alignment = 16;

    // An arena with no block.  A block size of zero ends the process.
    explicit BumpArena(std::size_t block_size = std::size_t{1} << 20) noexcept : block_size_{block_size} {
        if (block_size == 0) [[unlikely]] {
            fatal("a BumpArena of blocks of zero bytes: give a block size above zero");
        }
    }

    // Gives each block back to the allocation door.
    ~BumpArena();

    BumpArena(BumpArena const&) = delete("a BumpArena owns its blocks alone: a copy would free them two times");
    BumpArena&
    operator=(BumpArena const&) = delete("a BumpArena owns its blocks alone: a copy would free them two times");
    BumpArena(BumpArena&&) = delete("an epoch of a BumpArena names the arena: build a new arena instead");
    BumpArena& operator=(BumpArena&&) = delete("an epoch of a BumpArena names the arena: build a new arena instead");

    // One T that the arguments build in new bytes of the arena, at the
    // alignment of T.  The Ref has the fresh brand of the call.  The pack
    // refuses each explicit argument after T, so no caller names the brand.
    template <class T, class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND, class Marker,
              class... Args>
        requires(sizeof...(Refused) == 0 && CanMakeInArena<T, Marker, Args...>)
    [[nodiscard]] Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>> make(Marker /*allocation*/,
                                                                               Args&&... args) & noexcept {
        T* const object = ::new(bump_(sizeof(T), alignof(T))) T(static_cast<Args&&>(args)...);
        return detail::RefDoor::lend<Fresh>(*object);
    }

    template <class T, class... Rest>
    void make(Rest&&...) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // count copies of fill in new bytes of the arena, at the alignment of T.
    // A count of zero gives the empty View and takes no bytes.  A count past
    // the largest count of a View ends the process.  Complexity: O(count).
    template <class T, class Marker>
        requires CanFillArenaArray<T, Marker>
    [[nodiscard]] View<T> array(Marker /*allocation*/, std::size_t count, T const& fill) & noexcept {
        if (count == 0) {
            return View<T>{};
        }
        if (count > max_view_count(sizeof(T))) [[unlikely]] {
            detail::refuse_arena_array_(count, sizeof(T));
        }
        std::size_t const bytes =
            checked_mul(count, sizeof(T)).expect("the bytes of an array below the largest View fit in size_t");
        T* const first = static_cast<T*>(bump_(bytes, alignof(T)));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(fill);
        }
        return View<T>{detail::ViewDoor::key(), first, count};
    }

    template <class T, class Marker>
    void array(Marker, std::size_t,
               T const&) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // Calls body in an epoch, and gives its result.  The arena must hold no
    // bytes, so the epoch covers each byte that the arena gives out during
    // the body.  After the body, each of those bytes dies.
    template <class Body>
        requires EpochBody<Body>
    std::invoke_result_t<Body&&> with_epoch(Body&& body) & noexcept {
        open_epoch_();
        if constexpr (std::is_void_v<std::invoke_result_t<Body&&>>) {
            static_cast<Body&&>(body)();
            end_epoch_();
        } else {
            std::invoke_result_t<Body&&> result = static_cast<Body&&>(body)();
            end_epoch_();
            return result;
        }
    }

    template <class Body>
    void with_epoch(Body&&) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // The bytes of each block that the arena holds, also the blocks that it
    // keeps for the next epoch.
    [[nodiscard]] std::size_t retained_bytes() const noexcept { return retained_bytes_; }

private:
    // The bump: bytes bytes at the alignment, from the current block.  bytes
    // is above zero and at most __PTRDIFF_MAX__, and the alignment is a power
    // of two, so the sum below does not wrap.  An arena with no block has an
    // end of zero, so its first request takes the cold path.
    [[nodiscard, gnu::always_inline]] void* bump_(std::size_t bytes, std::size_t alignment) noexcept {
        std::uintptr_t const base = __builtin_bit_cast(std::uintptr_t, cur_block_);
        std::uintptr_t const aligned_address = (base + offset_ + alignment - 1) & ~(alignment - 1);
        std::size_t const aligned = aligned_address - base;
        if (aligned + bytes <= end_offset_) [[likely]] {
            std::byte* const storage = cur_block_ + aligned;
            offset_ = aligned + bytes;
            detail::bump_unpoison_(storage, bytes);
            return storage;
        }
        return bump_slow_(bytes, alignment);
    }

    // The bytes from the first block after the current block that holds the
    // request, or from a new block at the end of the list.
    [[nodiscard, gnu::cold, gnu::noinline, gnu::returns_nonnull]] void* bump_slow_(std::size_t bytes,
                                                                                   std::size_t alignment) noexcept;

    // Makes the first block from first_index that holds needed bytes the
    // current block.  A block of an earlier epoch serves again when it holds
    // needed bytes, and a smaller one stays for a later epoch.
    void enter_block_(std::uint32_t first_index, std::size_t needed) noexcept;

    // Gives each block from first_index to the end of the list back to the
    // allocation door.
    void free_blocks_from_(std::uint32_t first_index) noexcept;

    // The checks of an epoch at its start: no open epoch, and no byte given.
    void open_epoch_() noexcept;

    // Each byte that the arena gave out dies.  Above the limit, each block
    // but the first goes back to the door.  The blocks that stay are
    // poisoned, and the first block becomes the current block.
    void end_epoch_() noexcept;

    // The hot fields, read on each bump, share one cache line.
    std::byte* cur_block_ = nullptr;
    std::size_t offset_ = 0;
    std::size_t end_offset_ = 0;

    // The cold fields, read on the cold path, by the epoch and by the query.
    std::size_t block_size_ = 0;
    std::size_t retained_bytes_ = 0;
    std::uint32_t cur_index_ = 0;
    std::uint32_t block_count_ = 0;
    bool has_open_epoch_ = false;
    std::uint8_t pad_[7]{};
    // The blocks in the order of their allocation, and the bytes of each.
    std::byte* blocks_[max_blocks]{};
    std::size_t block_bytes_[max_blocks]{};
};

}  // namespace foundation::core
