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
//     takes no block in its steady state.  An arena that holds more than its
//     retained limit at the end gives back each block but the first.  The
//     limit is default_retained_bytes_limit until set_retained_limit sets a
//     different one.  An epoch in a different epoch of one arena, and an
//     epoch of an arena that holds bytes, end the process.
//
// The result of the body of an epoch holds no borrow (HoldsNoBorrow of
// foundation/core/Choice.h).  So a Ref, a View, an address, or a class that
// holds one does not leave the epoch through the result.  The concept
// refuses each borrow, also a borrow of bytes that the epoch does not end.
// Such a value leaves through a variable that the body captures.
//
// That capture is the one route that stays open: a body can write a borrow
// into the epoch to a variable that it captures.  A View has no brand, so
// no type refuses it there.  The check of that route is at run time: with
// AddressSanitizer, a new block and the end of an epoch poison each block,
// and each allocation unpoisons only the bytes that it gives
// (foundation/os/Poison.h).  A read of an ended epoch, or past the bytes of
// an allocation, then ends the process with a report.
//
// The cold path, the end of an epoch and the end of the arena are in
// src/foundation/os/BumpArena.cpp, which calls the allocation door and the
// poison door.  The poison door is in a layer above this header, so this
// header declares the one call that the bump needs.

#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Quantity.h>
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

// The body of an epoch: a call with no argument and no throw, of a callable
// that holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h).
template <class Body>
concept EpochBody = std::is_nothrow_invocable_v<Body&&> && ::foundation::linear::HoldsNoLinearValue<Body>;

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

// Ends the process for a next address of an arena that does not fit in the
// address space at the alignment.  The report is out of line, as the report
// of refuse_arena_array_ is, so the frame of the bump holds no stack canary.
[[noreturn, gnu::cold, gnu::noinline]] void refuse_arena_address_(std::size_t alignment) noexcept;

// The address at the alignment, or the end of the process when the rounding
// does not fit in the address space.  An alignment of a type is at most the
// size of half the address space.  On x86_64, bit 63 of a user address is
// zero, also with linear address masking, so the optimizer reads that the
// rounding does not wrap, and the test of the Option folds away.  On aarch64
// the top byte of an address can hold a tag (TBI, MTE, HWASan), so an
// address can be above __PTRDIFF_MAX__, and that arm makes no assumption
// about the address.  The refusal is out of line.
[[nodiscard, gnu::always_inline]] inline std::uintptr_t arena_aligned_(std::uintptr_t address,
                                                                       std::size_t alignment) noexcept {
#if defined(__x86_64__)
    [[assume(address <= static_cast<std::uintptr_t>(__PTRDIFF_MAX__))]];
#endif
    [[assume(alignment <= static_cast<std::size_t>(__PTRDIFF_MAX__))]];
    Option<std::uintptr_t> rounded = align_up(address, std::uintptr_t{alignment});
    if (rounded.is_none()) [[unlikely]] {
        refuse_arena_address_(alignment);
    }
    return std::move(rounded).value_or(0);
}

}  // namespace detail

class BumpArena;

// The door of a BumpArena to the raw constructors of a View (the doors of the
// owners above this header in foundation/core/Region.h).  The invariant: the
// bump of array() gave count * sizeof(T) bytes at the alignment of T, and its
// loop built each element.  So the View of the count elements holds live
// elements only.  The View holds no brand of the arena, so a View that
// outlives the epoch of its bytes is a known gap, which a brand of the arena
// closes.  BumpArena is the one friend, and a home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ArenaArrayDoor final : ::foundation::NoObject<ArenaArrayDoor> {
    friend class BumpArena;

    // The count elements that array() built from first.
    template <class T>
    [[nodiscard]] static View<T> built_run_(T* first, std::size_t count) noexcept {
        return View<T>{first, count};
    }
};

// The arena owns its blocks, and no door stores a pointer or a borrow that a
// caller gives, so a body that captures the arena stores no borrow in it
// (owns_its_storage of foundation/core/Owner.h).
class[[= owns_its_storage{}]] BumpArena final {
public:
    // The bytes of blocks that an arena keeps when an epoch ends, until
    // set_retained_limit sets a different limit.  An arena that holds more
    // gives back each block but the first, so one epoch with an unusual peak
    // does not pin its peak for the life of the arena.
    static constexpr std::size_t default_retained_bytes_limit = std::size_t{64} << 20;

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
        return ArenaArrayDoor::built_run_(first, count);
    }

    template <class T, class Marker>
    void array(Marker, std::size_t,
               T const&) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // Calls body in an epoch, and gives its result.  The arena must hold no
    // bytes, so the epoch covers each byte that the arena gives out during
    // the body.  After the body, each of those bytes dies, so the result
    // holds no borrow.
    template <class Body>
        requires EpochBody<Body> && HoldsNoBorrow<std::invoke_result_t<Body&&>>
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

    // A body whose result holds a borrow binds to this overload, so the
    // refusal gives its reason.
    template <class Body>
        requires EpochBody<Body> && (!HoldsNoBorrow<std::invoke_result_t<Body &&>>)
    void with_epoch(Body&&) & = delete(
        "the result of an epoch holds no borrow, because the end of the epoch ends each byte that the arena gave out "
        "during the body: give a value with no reference and no address, or write a value that owns its storage to "
        "a variable that the body captures");

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason.
    template <class Body>
        requires std::is_invocable_v<Body&&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_epoch(Body&&) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    template <class Body>
    void with_epoch(Body&&) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // The bytes of each block that the arena holds, also the blocks that it
    // keeps for the next epoch.
    [[nodiscard]] std::size_t retained_bytes() const noexcept { return retained_bytes_; }

    // The bytes of blocks that the arena keeps when an epoch ends.  The end
    // of each later epoch reads the limit.  A limit of zero keeps only the
    // first block.
    void set_retained_limit(Quantity<unit::Byte, std::size_t> limit) & noexcept { retained_limit_ = limit.raw(); }

    [[nodiscard]] Quantity<unit::Byte, std::size_t> retained_limit() const noexcept {
        return Quantity<unit::Byte, std::size_t>{retained_limit_};
    }

private:
    // The bump: bytes bytes at the alignment, from the current block.  bytes
    // is above zero and at most __PTRDIFF_MAX__, and the alignment is a power
    // of two, so the rounding below does not wrap.  An arena with no block has
    // an end of zero, so its first request takes the cold path.
    [[nodiscard, gnu::always_inline]] void* bump_(std::size_t bytes, std::size_t alignment) noexcept {
        std::uintptr_t const base = __builtin_bit_cast(std::uintptr_t, cur_block_);
        std::uintptr_t const aligned_address = detail::arena_aligned_(base + offset_, alignment);
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

    // Each byte that the arena gave out dies.  Above the retained limit, each
    // block but the first goes back to the door.  The blocks that stay are
    // poisoned, and the first block becomes the current block.
    void end_epoch_() noexcept;

    // The hot fields, read on each bump, share one cache line.
    std::byte* cur_block_ = nullptr;
    std::size_t offset_ = 0;
    std::size_t end_offset_ = 0;

    // The cold fields, read on the cold path, by the epoch and by the query.
    std::size_t block_size_ = 0;
    std::size_t retained_bytes_ = 0;
    std::size_t retained_limit_ = default_retained_bytes_limit;
    std::uint32_t cur_index_ = 0;
    std::uint32_t block_count_ = 0;
    bool has_open_epoch_ = false;
    std::uint8_t pad_[7]{};
    // The blocks in the order of their allocation, and the bytes of each.
    std::byte* blocks_[max_blocks]{};
    std::size_t block_bytes_[max_blocks]{};
};

}  // namespace foundation::core
