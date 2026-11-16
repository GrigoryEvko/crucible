#pragma once

// An owned block of bytes at an alignment that the run gives, such as a pool
// that takes the alignment of a huge page only when it holds at least one
// huge page.  A HeapArray of foundation/core/Array.h fixes its alignment in
// its type and holds typed elements.
//
// No operation of the class writes a byte of the block, so the mint of a
// block of many gigabytes writes no page.  A byte holds no value until the
// owner of the block writes it.  fill() of the Region family writes a value
// into each byte of the View.
//
// mint_aligned_bytes(alloc, bytes, alignment) is the one door.  The
// allocation capability comes first, so each allocation shows at its call.
// A count of zero gives the empty block and allocates nothing.  An alignment
// that is not a power of two is a defect of the caller, and the precondition
// ends the process.  The block takes its bytes rounded up to the alignment,
// and a round up that wraps ends the process.  The allocation door of
// foundation/core/Heap.h ends the process when the memory is exhausted, as
// for mint_heap_array.
//
// The block owns its bytes alone.  A move gives them to the target and
// leaves the source empty, and the destructor gives them back to the
// allocation door.  view() lends the bytes as a View, and no member gives a
// raw pointer.
//
// The header also holds the arithmetic of an aligned allocation, which the
// block and SwissTableBuffer share, and the size of one huge page.  The
// alignment of a huge page is necessary for the kernel to back a region with
// huge pages, and not sufficient: nothing here advises the kernel.

#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace foundation {

// The size of one huge page on the supported targets: x86-64, and aarch64
// with 4 KiB base pages.
inline constexpr std::size_t huge_page_bytes = std::size_t{2} << 20;

namespace detail {

// The fatal exit of a size that wraps.  The text names the entry of the
// diagnostic catalog (foundation/diag/Catalog.h) that gives the causes and
// the remediation.  The allocation door of foundation/core/Heap.h has the
// fatal exit of an exhausted memory.
[[noreturn]] CRUCIBLE_COLD inline void allocation_size_overflow_abort_(std::size_t prefix_bytes, std::size_t count,
                                                                       std::size_t element_bytes,
                                                                       std::size_t alignment) noexcept {
    ::foundation::core::fatal("{} prefix bytes and {} elements of {} bytes, rounded to {}, do not fit in size_t.  The "
                              "catalog entry foundation::diag::AllocationSizeOverflow gives the remediation",
                              prefix_bytes, count, element_bytes, alignment);
}

// The bytes of one allocation: a prefix, then `count` elements of
// `element_bytes` each, rounded up to the alignment.  Each step wraps on a
// count that no real allocation has, and a wrap would hand back a block
// smaller than asked for, so a wrap aborts.  The checks are plain code and
// not contract clauses, so they hold under every contract evaluation
// semantic.  SwissTableBuffer computes its size here.  Complexity: O(1).
template <std::size_t Alignment>
    requires(core::has_single_bit(Alignment))
[[nodiscard]] constexpr std::size_t aligned_allocation_bytes(std::size_t prefix_bytes, std::size_t count,
                                                             std::size_t element_bytes) noexcept {
    std::size_t element_total = 0;
    std::size_t total = 0;
    const bool wraps = __builtin_mul_overflow(count, element_bytes, &element_total)
                    || __builtin_add_overflow(prefix_bytes, element_total, &total);
    core::Option<std::size_t> rounded = core::align_up(total, Alignment);
    if (wraps || rounded.is_none()) [[unlikely]]
        allocation_size_overflow_abort_(prefix_bytes, count, element_bytes, Alignment);
    return std::move(rounded).value_or(0);
}

// The door of mint_aligned_bytes, and the key of the constructor of an
// AlignedBytes.  AlignedBytesDoor is the one friend of the key, and only
// mint_aligned_bytes calls the door.  The door and the key hold no template,
// so no explicit specialization of a template reaches the key.
struct AlignedBytesDoor;

class [[nodiscard]] AlignedBytesKey final {
    friend struct AlignedBytesDoor;

    constexpr AlignedBytesKey() noexcept {}
    constexpr AlignedBytesKey(AlignedBytesKey const&) noexcept {}

public:
    AlignedBytesKey& operator=(AlignedBytesKey const&) = delete("a key of an AlignedBytes lives for one construction");
};

struct AlignedBytesDoor {
    [[nodiscard]] static constexpr AlignedBytesKey key() noexcept { return AlignedBytesKey{}; }
};

// The bytes that a block of `bytes` bytes takes at the alignment: the count
// rounded up to the alignment.  The caller gives a power of two.  A round up
// that wraps ends the process through the fatal exit of a size that wraps.
// Complexity: O(1).
[[nodiscard]] constexpr std::size_t aligned_block_bytes(std::size_t bytes, std::size_t alignment) noexcept {
    core::Option<std::size_t> rounded = core::align_up(bytes, alignment);
    if (rounded.is_none()) [[unlikely]]
        allocation_size_overflow_abort_(0, bytes, 1, alignment);
    return std::move(rounded).value_or(0);
}

}  // namespace detail

namespace core::detail::owner {

// The fields of an AlignedBytes: the first byte, the count of bytes and the
// alignment, or null and zeros when it holds no block.
struct AlignedBytesState {
    std::uint8_t* first = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;

    [[nodiscard]] static constexpr AlignedBytesState emptied(AlignedBytesState const& /*state*/) noexcept {
        return AlignedBytesState{};
    }

    // Gives the block back to the allocation door with the bytes and the
    // alignment of the mint.
    static constexpr void release(AlignedBytesState const& state) noexcept {
        if (state.first != nullptr) {
            heap_release_(state.first, ::foundation::detail::aligned_block_bytes(state.bytes, state.alignment),
                          state.alignment);
        }
    }
};

}  // namespace core::detail::owner

// The core of foundation/core/Owner.h gives the moves and the end of a
// block.
class [[nodiscard]] AlignedBytes final : public core::detail::OwnerCore<core::detail::owner::AlignedBytesState> {
    using State = core::detail::owner::AlignedBytesState;

public:
    // The empty block: no byte and no storage.
    constexpr AlignedBytes() noexcept = default;

    // A block of `bytes` bytes at the alignment, with the key that only
    // AlignedBytesDoor builds.  A count of zero gives the empty block and
    // allocates nothing.  The bytes start their lifetime with no value and
    // with no write.  Complexity: one allocation.
    AlignedBytes(detail::AlignedBytesKey const& /*key*/, std::size_t bytes, std::size_t alignment) noexcept {
        CRUCIBLE_PRE(core::has_single_bit(alignment));
        if (bytes == 0) return;
        // The allocation door gives storage of the allocator of the C
        // library, which starts the lifetime of an array of bytes with no
        // write.
        state_ = State{static_cast<std::uint8_t*>(
                           core::detail::heap_allocate_(detail::aligned_block_bytes(bytes, alignment), alignment)),
                       bytes, alignment};
    }

    AlignedBytes(AlignedBytes const&) =
        delete("an AlignedBytes owns its block alone: a copy would give it back two times");
    AlignedBytes& operator=(AlignedBytes const&) =
        delete("an AlignedBytes owns its block alone: a copy would give it back two times");

    // The assignment gives the block of this owner back after it takes the
    // block of other.
    AlignedBytes(AlignedBytes&&) noexcept = default;
    AlignedBytes& operator=(AlignedBytes&&) noexcept = default;

    // The end of the block and reset() are constexpr, so a class with an
    // empty block member stays a literal type.  No constant evaluation
    // allocates, so none reaches the allocation door.  reset() gives the
    // block back and leaves the empty block, and the block is empty before
    // the release starts.
    constexpr void reset() noexcept { reset_(); }

    // The count of bytes that the mint asked for, without the round up.
    [[nodiscard]] std::size_t size() const noexcept { return state_.bytes; }

    // The alignment of the first byte, or zero for the empty block.
    [[nodiscard]] std::size_t alignment() const noexcept { return state_.alignment; }

    // The View of the bytes.  It borrows this block.  The storage is not
    // null for each count above zero, and the allocation door gives no block
    // of more than __PTRDIFF_MAX__ bytes, so the View comes through the door
    // of the base layer, with no check.
    [[nodiscard]] core::View<std::uint8_t> view() & noexcept {
        return core::View<std::uint8_t>{core::detail::ViewDoor::key(), state_.first, state_.bytes};
    }
    [[nodiscard]] core::View<std::uint8_t const> view() const& noexcept {
        return core::View<std::uint8_t const>{core::detail::ViewDoor::key(), state_.first, state_.bytes};
    }
    void
    view() && = delete("a view of a temporary AlignedBytes points into a block that the full expression gives back");
    void view() const&& =
        delete("a view of a temporary AlignedBytes points into a block that the full expression gives back");
};

// The gate of mint_aligned_bytes: the allocation capability, and no other
// tag.
template <class Marker>
concept CanMintAlignedBytes = core::AllocationCapability<Marker>;

// The one door of an AlignedBytes: a block of `bytes` bytes at the
// alignment, in new storage of the allocation door.  The allocation
// capability comes first, so each allocation shows at its call.
// Complexity: one allocation, and no write of a byte.
template <class Marker>
    requires CanMintAlignedBytes<Marker>
[[nodiscard]] AlignedBytes mint_aligned_bytes(Marker /*allocation*/, std::size_t bytes,
                                              std::size_t alignment) noexcept {
    return AlignedBytes{detail::AlignedBytesDoor::key(), bytes, alignment};
}

}  // namespace foundation
