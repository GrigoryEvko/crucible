#pragma once

// An owned block of bytes at an alignment that the run gives, such as a pool
// that takes the alignment of a huge page only when it holds at least one
// huge page.  A HeapArray of foundation/core/Array.h fixes its alignment in
// its type and holds typed elements.
//
// THE TWO STATES
//   UnwrittenBytes  a block whose bytes hold no value.  It has no view, so no
//                   code reads a byte before its write.  Two doors write each
//                   byte and give the block as an AlignedBytes.  fill(value)
//                   writes value into each byte.  write(writer) lends the
//                   WriteLease of foundation/core/Array.h, which writes the
//                   bytes in order and has no read.  A writer that leaves a
//                   byte with no write, or that gives a byte past the last
//                   one, ends the process
//   AlignedBytes    a block whose bytes each hold a value.  view() lends them
//                   as a View, and no member gives a raw pointer
//
// THE MINTS
//   mint_aligned_bytes(alloc, bytes, alignment)
//                   a new UnwrittenBytes.  No operation writes a byte, so the
//                   mint of a block of many gigabytes writes no page
//   mint_aligned_bytes_zeroed(alloc, bytes, alignment)
//                   a new AlignedBytes whose bytes each read zero.  The zeroed
//                   door of foundation/core/Heap.h takes a large block from
//                   fresh pages of the kernel, so it writes no page either,
//                   except under ThreadSanitizer, whose calloc writes each
//                   byte
// The allocation capability comes first, so each allocation shows at its
// call.  A count of zero gives the empty block and allocates nothing.  An
// alignment that is not a power of two is a defect of the caller, and the
// precondition ends the process.  The allocation door ends the process when
// the memory is exhausted, as for mint_heap_array.
//
// THE STORAGE
//   A block takes its bytes rounded up to the alignment, and alignment - 1
//   bytes more, at the alignment of the allocator of the C library.  Its
//   first byte is the first address at the alignment in that storage.  So the
//   two mints share one release, and the zeroed door needs no aligned form.
//   A sum that wraps ends the process.
//
// A block owns its bytes alone.  A move gives them to the target and leaves
// the source empty, and the destructor gives them back to the allocation
// door.
//
// The header also holds the arithmetic of an aligned allocation, which the
// block and SwissTableBuffer share, and the size of one huge page.  The
// alignment of a huge page is necessary for the kernel to back a region with
// huge pages, and not sufficient: nothing here advises the kernel.

#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
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

// The fatal exit of a writer of UnwrittenBytes::write that left bytes with
// no write, or that gave bytes past the last one.
[[noreturn]] CRUCIBLE_COLD inline void refuse_written_bytes_(std::size_t bytes, std::size_t unwritten,
                                                             std::size_t refused) noexcept {
    ::foundation::core::fatal("the writer of an UnwrittenBytes left {} of {} bytes with no write, and gave {} bytes "
                              "past the last one",
                              unwritten, bytes, refused);
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

// The door of the two mints, and the key of the constructors of an
// UnwrittenBytes and of an AlignedBytes.  AlignedBytesDoor is the one friend
// of the key, and only the mints and the write doors of an UnwrittenBytes
// call the door.  The door and the key hold no template, so no explicit
// specialization of a template reaches the key.
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

// The bytes that the storage of a block holds (THE STORAGE): the block
// rounded up to the alignment, and alignment - 1 bytes more.  The caller
// gives a power of two.  The rounded count is a multiple of the alignment,
// so it is at most SIZE_MAX + 1 - alignment, and the sum does not wrap.
// Complexity: O(1).
[[nodiscard]] constexpr std::size_t stored_block_bytes(std::size_t bytes, std::size_t alignment) noexcept {
    return aligned_block_bytes(bytes, alignment) + (alignment - 1);
}

// The door of the allocation door that gives the storage of a block: the
// door whose bytes hold no value, or the door whose bytes read zero.
enum class BlockStorage : std::uint8_t {
    unwritten,
    zeroed
};

// The count of bytes from the first byte of the storage to the first byte of
// the block: the distance to the first address at the alignment (THE
// STORAGE).  The caller gives a power of two, and the count is below it.  The
// supported targets are 64-bit, so an address is a size_t.  Complexity: O(1).
[[nodiscard]] inline std::size_t block_lead_bytes(void const* storage, std::size_t alignment) noexcept {
    std::size_t const address = __builtin_bit_cast(std::size_t, storage);
    return (std::size_t{0} - address) & (alignment - 1);
}

}  // namespace detail

namespace core::detail::owner {

// The fields of a block: the storage from the allocation door, the count of
// bytes and the alignment, or null and zeros when it holds no block.  The two
// states of a block share the fields.
struct AlignedBytesState {
    using owner_state_mark = OwnerStateMark;

    std::uint8_t* storage = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;

    [[nodiscard]] static constexpr AlignedBytesState emptied(AlignedBytesState const& /*state*/) noexcept {
        return AlignedBytesState{};
    }

    // The storage of a block of `bytes` bytes at the alignment, from the door
    // of the kind (THE STORAGE).  A count of zero gives the empty fields and
    // allocates nothing.  Complexity: one allocation.
    [[nodiscard]] static AlignedBytesState allocated(std::size_t bytes, std::size_t alignment,
                                                     ::foundation::detail::BlockStorage kind) noexcept {
        CRUCIBLE_PRE(core::has_single_bit(alignment));
        if (bytes == 0) return AlignedBytesState{};
        std::size_t const stored = ::foundation::detail::stored_block_bytes(bytes, alignment);
        void* const taken = kind == ::foundation::detail::BlockStorage::zeroed ? heap_allocate_zeroed_(stored)
                                                                               : heap_allocate_(stored, 1);
        return AlignedBytesState{static_cast<std::uint8_t*>(taken), bytes, alignment};
    }

    // Gives the storage back to the allocation door with the bytes of the
    // storage and an alignment of one, as the two doors of a block take it.
    static constexpr void release(AlignedBytesState const& state) noexcept {
        if (state.storage != nullptr) {
            heap_release_(state.storage, ::foundation::detail::stored_block_bytes(state.bytes, state.alignment), 1);
        }
    }
};

}  // namespace core::detail::owner

// A block whose bytes each hold a value.  The core of foundation/core/Owner.h
// gives the moves and the end of a block.
class [[nodiscard]] AlignedBytes final : public core::OwnerCore<core::detail::owner::AlignedBytesState> {
    using State = core::detail::owner::AlignedBytesState;

    // The first byte of the block: the first address at the alignment in the
    // storage, or null for the empty block.  The storage holds alignment - 1
    // bytes past the block, so the block ends inside it.  Complexity: O(1).
    [[nodiscard]] std::uint8_t* first_() const noexcept {
        if (state_.storage == nullptr) return nullptr;
        return state_.storage + detail::block_lead_bytes(state_.storage, state_.alignment);
    }

public:
    // The empty block: no byte and no storage.
    constexpr AlignedBytes() noexcept = default;

    // The block of the fields, with the key that only AlignedBytesDoor
    // builds.  The caller gives fields whose bytes each hold a value: fields
    // of the zeroed door, or fields whose bytes a door of an UnwrittenBytes
    // wrote.
    AlignedBytes(detail::AlignedBytesKey const& /*key*/, State const& state) noexcept : core::OwnerCore<State>{state} {}

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

    // The View of the bytes.  It borrows this block.  The first byte is not
    // null for each count above zero, and the allocation door gives no block
    // of more than __PTRDIFF_MAX__ bytes, so the View comes through the door
    // of the base layer, with no check.
    [[nodiscard]] core::View<std::uint8_t> view() & noexcept {
        return core::View<std::uint8_t>{core::detail::ViewDoor::key(), first_(), state_.bytes};
    }
    [[nodiscard]] core::View<std::uint8_t const> view() const& noexcept {
        return core::View<std::uint8_t const>{core::detail::ViewDoor::key(), first_(), state_.bytes};
    }
    void
    view() && = delete("a view of a temporary AlignedBytes points into a block that the full expression gives back");
    void view() const&& =
        delete("a view of a temporary AlignedBytes points into a block that the full expression gives back");
};

// A block whose bytes hold no value.  It has no view and no read, so no code
// reads a byte before its write.  The core of foundation/core/Owner.h gives
// the moves and the end of a block.
class [[nodiscard]] UnwrittenBytes final : public core::OwnerCore<core::detail::owner::AlignedBytesState> {
    using State = core::detail::owner::AlignedBytesState;

    // Gives the fields to the caller and leaves this block empty.
    [[nodiscard]] State give_up_() noexcept {
        State const given = state_;
        state_ = State::emptied(given);
        return given;
    }

    // The View of the count bytes from the first byte of the block of the
    // fields, for a door that writes each byte (THE STORAGE).  The View is no
    // part of the surface, so no read reaches a byte with no value.
    // Complexity: O(1).
    [[nodiscard]] static core::View<std::uint8_t> written_run_(State const& given) noexcept {
        std::uint8_t* const first = given.storage == nullptr
                                      ? nullptr
                                      : given.storage + detail::block_lead_bytes(given.storage, given.alignment);
        return core::View<std::uint8_t>{core::detail::ViewDoor::key(), first, given.bytes};
    }

public:
    // The empty block: no byte and no storage.
    constexpr UnwrittenBytes() noexcept = default;

    // A block of `bytes` bytes at the alignment, with the key that only
    // AlignedBytesDoor builds.  A count of zero gives the empty block and
    // allocates nothing.  The bytes start their lifetime with no value and
    // with no write.  Complexity: one allocation.
    UnwrittenBytes(detail::AlignedBytesKey const& /*key*/, std::size_t bytes, std::size_t alignment) noexcept
        : core::OwnerCore<State>{State::allocated(bytes, alignment, detail::BlockStorage::unwritten)} {}

    UnwrittenBytes(UnwrittenBytes const&) =
        delete("an UnwrittenBytes owns its block alone: a copy would give it back two times");
    UnwrittenBytes& operator=(UnwrittenBytes const&) =
        delete("an UnwrittenBytes owns its block alone: a copy would give it back two times");
    UnwrittenBytes(UnwrittenBytes&&) noexcept = default;
    UnwrittenBytes& operator=(UnwrittenBytes&&) noexcept = default;

    // The count of bytes that the mint asked for, without the round up.
    [[nodiscard]] std::size_t size() const noexcept { return state_.bytes; }

    // The alignment of the first byte, or zero for the empty block.
    [[nodiscard]] std::size_t alignment() const noexcept { return state_.alignment; }

    // Writes value into each byte, and gives the block as an AlignedBytes.
    // This block is then empty.  Complexity: O(size()).
    [[nodiscard]] AlignedBytes fill(std::uint8_t value) && noexcept {
        State const given = give_up_();
        core::fill(written_run_(given), value);
        return AlignedBytes{detail::AlignedBytesDoor::key(), given};
    }

    // writer(lease) writes each byte in order through a WriteLease, which has
    // no read, and the block then comes back as an AlignedBytes.  This block
    // is then empty.  A writer that leaves a byte with no write, or that gives
    // a byte past the last one, ends the process, and the AlignedBytes then
    // never exists.  Complexity: the writes of the writer.
    template <class Writer>
        requires std::is_nothrow_invocable_v<Writer&, core::WriteLease<std::uint8_t>&>
    [[nodiscard]] AlignedBytes write(Writer writer) && noexcept {
        State const given = give_up_();
        core::View<std::uint8_t> const run = written_run_(given);
        core::WriteLease<std::uint8_t> lease{core::detail::HeapArrayDoor::key(),
                                             first_of_(core::detail::ViewDoor::key(), run), given.bytes};
        writer(lease);
        if (lease.remaining() != 0 || lease.refused() != 0) [[unlikely]] {
            detail::refuse_written_bytes_(given.bytes, lease.remaining(), lease.refused());
        }
        return AlignedBytes{detail::AlignedBytesDoor::key(), given};
    }

    // A named block stays with the caller, so a door that gives it away
    // takes an rvalue.  A read of the bytes before a write has no door.
    void fill(std::uint8_t value) & = delete("fill gives the block away as an AlignedBytes: fill std::move(block)");
    template <class Writer>
    void write(Writer writer) & = delete("write gives the block away as an AlignedBytes: write std::move(block)");
    void view() const = delete("the bytes of an UnwrittenBytes hold no value: fill or write them first, or take the "
                               "block from mint_aligned_bytes_zeroed");
};

// The gate of the two mints: the allocation capability, and no other tag.
template <class Marker>
concept CanMintAlignedBytes = core::AllocationCapability<Marker>;

// A block of `bytes` bytes at the alignment, in new storage of the
// allocation door, whose bytes hold no value.  The allocation capability
// comes first, so each allocation shows at its call.  Complexity: one
// allocation, and no write of a byte.
template <class Marker>
    requires CanMintAlignedBytes<Marker>
[[nodiscard]] UnwrittenBytes mint_aligned_bytes(Marker /*allocation*/, std::size_t bytes,
                                                std::size_t alignment) noexcept {
    return UnwrittenBytes{detail::AlignedBytesDoor::key(), bytes, alignment};
}

// A block of `bytes` bytes at the alignment, in new storage of the zeroed
// allocation door, whose bytes each read zero.  Complexity: one allocation.
// A large block comes from fresh pages of the kernel, so the mint writes no
// page of it, except under ThreadSanitizer (THE MINTS).
template <class Marker>
    requires CanMintAlignedBytes<Marker>
[[nodiscard]] AlignedBytes mint_aligned_bytes_zeroed(Marker /*allocation*/, std::size_t bytes,
                                                     std::size_t alignment) noexcept {
    return AlignedBytes{detail::AlignedBytesDoor::key(), core::detail::owner::AlignedBytesState::allocated(
                                                             bytes, alignment, detail::BlockStorage::zeroed)};
}

}  // namespace foundation
