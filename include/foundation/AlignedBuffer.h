#pragma once

// An owned allocation of `count` elements of T at an alignment of the
// caller's choice.  The buffer owns the storage alone: a move hands it on and
// leaves the source empty, and the destructor frees it.  T is trivially
// destructible, so freeing the storage ends the elements.
//
// Each element starts its lifetime in one of two ways, and each allocation
// route states what it needs:
//
//   allocate                     the checked start over the bytes, so T is
//                                an implicit-lifetime type throughout
//   allocate_value_initialized   a value initialization of each element, so
//                                T has a default constructor that does not
//                                throw
//
// A proof type, or a class that holds one, has neither.  A class that
// refuses a start over bytes, such as a provenance tag, can still hold a
// value that its own default constructor built.
//
// A huge-page buffer is this buffer at huge_page_bytes alignment.  The
// alignment is necessary for the kernel to back the region with huge pages
// and not sufficient: nothing here advises the kernel.  A caller that wants
// huge pages registers the region and advises it separately.
//
// AlignedBytes, at the end of this header, owns a block of bytes at an
// alignment that the run gives, and its mint writes no byte.

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

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
// `element_bytes` each, rounded up to the alignment.  Each step wraps on a count that no real allocation has, and
// a wrap would hand back a block smaller than asked for, so a wrap
// aborts.  The checks are plain code and not contract clauses, so they
// hold under every contract evaluation semantic.  AlignedBuffer and
// SwissTableBuffer compute their sizes here.  Complexity: O(1).
template <std::size_t Alignment>
    requires(std::has_single_bit(Alignment))
[[nodiscard]] constexpr std::size_t aligned_allocation_bytes(std::size_t prefix_bytes, std::size_t count,
                                                             std::size_t element_bytes) noexcept {
    std::size_t element_total = 0;
    std::size_t total = 0;
    std::size_t padded = 0;
    const bool wraps = __builtin_mul_overflow(count, element_bytes, &element_total)
                    || __builtin_add_overflow(prefix_bytes, element_total, &total)
                    || __builtin_add_overflow(total, Alignment - 1, &padded);
    if (wraps) [[unlikely]]
        allocation_size_overflow_abort_(prefix_bytes, count, element_bytes, Alignment);
    return padded & ~(Alignment - 1);
}

}  // namespace detail

namespace core::detail::owner {

// The fields of an AlignedBuffer: the first element and the count, or null
// and zero when it holds no storage.
template <typename T, std::size_t Alignment>
struct AlignedBufferState {
    T* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] static constexpr AlignedBufferState emptied(AlignedBufferState const& /*state*/) noexcept {
        return AlignedBufferState{};
    }

    // Gives the storage back.  T is trivially destructible, so the release
    // ends the elements.
    static constexpr void release(AlignedBufferState const& state) noexcept {
        if (state.data != nullptr) {
            heap_release_(state.data,
                          ::foundation::detail::aligned_allocation_bytes<Alignment>(0, state.size, sizeof(T)),
                          Alignment);
        }
    }
};

}  // namespace core::detail::owner

// The core of foundation/core/Owner.h gives the moves and the end of a
// buffer.
template <typename T, std::size_t Alignment = alignof(T)>
    requires(std::is_object_v<T> && std::is_trivially_destructible_v<T> && std::has_single_bit(Alignment)
             && Alignment >= alignof(T))
class [[nodiscard]] AlignedBuffer final
    : public core::detail::OwnerCore<core::detail::owner::AlignedBufferState<T, Alignment>> {
    using State = core::detail::owner::AlignedBufferState<T, Alignment>;
    using Core = core::detail::OwnerCore<State>;

public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::AlignedBuffer"; }

    constexpr AlignedBuffer() noexcept = default;

    // The bytes the allocation of `count` elements occupies: the element
    // bytes rounded up to the alignment.  A wrap ends the process through
    // the fatal exit, and the text names the catalog entry
    // AllocationSizeOverflow.
    [[nodiscard]] static constexpr size_type allocation_bytes(size_type count) noexcept {
        return detail::aligned_allocation_bytes<Alignment>(0, count, sizeof(T));
    }

    // Starts the lifetime of `count` elements over fresh storage.  An
    // element holds an erroneous value until it is written.  Exhaustion
    // aborts: this runs where a failed allocation has no recovery.
    [[nodiscard]] static AlignedBuffer allocate(size_type count)
        requires lifetime::ImplicitLifetimeThroughout<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        void* const raw = allocate_storage_(count);
        return AlignedBuffer{lifetime::start_as_array<T>(raw, count).data(), count};
    }

    // Value-initializes each element, so a T with member initializers gets
    // the values it declares, and every other T reads zero.  Complexity:
    // linear in count.
    [[nodiscard]] static AlignedBuffer allocate_value_initialized(size_type count)
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        T* const first = static_cast<T*>(allocate_storage_(count));
        for (size_type index = 0; index < count; ++index)
            ::new(static_cast<void*>(first + index)) T();
        return AlignedBuffer{first, count};
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");

    AlignedBuffer(AlignedBuffer&&) noexcept = default;
    AlignedBuffer& operator=(AlignedBuffer&&) noexcept = default;

    // The end of the buffer and reset() are constexpr, so a class with an
    // empty buffer member stays a literal type.  No constant evaluation
    // allocates, because allocate() is not constexpr, so it never reaches the
    // free.  The buffer is empty before the release starts.
    constexpr void reset() noexcept { this->reset_(); }

    [[nodiscard]] T* data() noexcept { return this->state_.data; }
    [[nodiscard]] const T* data() const noexcept { return this->state_.data; }

    [[nodiscard]] size_type size() const noexcept { return this->state_.size; }
    [[nodiscard]] size_type bytes() const noexcept { return size() == 0 ? 0 : allocation_bytes(size()); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return this->state_.data != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{this->state_.data, this->state_.size}; }
    [[nodiscard]] std::span<const T> span() const noexcept {
        return std::span<const T>{this->state_.data, this->state_.size};
    }

    // The View of the elements.  It borrows this buffer.  The storage is not
    // null for each count above zero, and the allocation door gives no block
    // of more than __PTRDIFF_MAX__ bytes, so the count is at most the largest
    // count of a View.  The View then comes through the door of the base
    // layer, with no check.  The return type is deduced, so a unit that
    // only names the class, such as a concept of a gate, does not read View.
    [[nodiscard]] auto view() & noexcept {
        return core::View<T>{core::detail::ViewDoor::key(), this->state_.data, this->state_.size};
    }
    [[nodiscard]] auto view() const& noexcept {
        return core::View<T const>{core::detail::ViewDoor::key(), this->state_.data, this->state_.size};
    }
    void view() && = delete("a view of a temporary AlignedBuffer points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary AlignedBuffer points into storage that the full expression frees");

    [[nodiscard]] T& operator[](size_type i) noexcept {
        CRUCIBLE_PRE(i < this->state_.size);
        return this->state_.data[i];
    }
    [[nodiscard]] const T& operator[](size_type i) const noexcept {
        CRUCIBLE_PRE(i < this->state_.size);
        return this->state_.data[i];
    }

private:
    explicit AlignedBuffer(T* data, size_type size) noexcept : Core{State{data, size}} {}

    // The allocation door ends the process when the memory is exhausted:
    // this runs where a failed allocation has no recovery.
    [[nodiscard]] static void* allocate_storage_(size_type count) {
        return core::detail::heap_allocate_(allocation_bytes(count), Alignment);
    }
};

// ── AlignedBytes ───────────────────────────────────────────────────────
//
// An owned block of bytes at an alignment that the run gives, such as a pool
// that takes the alignment of a huge page only when it holds at least one
// huge page.  AlignedBuffer fixes its alignment in its type.
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

namespace detail {

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
// rounded up to the alignment.  A round up that wraps ends the process
// through the fatal exit of a size that wraps.  Complexity: O(1).
[[nodiscard]] constexpr std::size_t aligned_block_bytes(std::size_t bytes, std::size_t alignment) noexcept {
    std::size_t padded = 0;
    if (__builtin_add_overflow(bytes, alignment - 1, &padded)) [[unlikely]]
        allocation_size_overflow_abort_(0, bytes, 1, alignment);
    return padded & ~(alignment - 1);
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
