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
//   mint_aligned_bytes(alloc, layout)
//                   a new UnwrittenBytes.  No operation writes a byte, so the
//                   mint of a block of many gigabytes writes no page
//   mint_aligned_bytes_zeroed(alloc, layout)
//                   a new AlignedBytes whose bytes each read zero.  The zeroed
//                   door of foundation/core/Heap.h takes a large block from
//                   fresh pages of the kernel, so it writes no page either,
//                   except under ThreadSanitizer, whose calloc writes each
//                   byte
// The allocation capability comes first, so each allocation shows at its
// call.  The Layout of foundation/alloc/Layout.h gives the bytes and the
// alignment of the block, so the two cannot swap, the bytes are above zero
// and the alignment is a power of two.  Each mint thus allocates, and the
// empty block is the default constructor of each state.  The allocation door
// ends the process when the memory is exhausted, as for mint_heap_array.
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
// THE WINDOWS
//   with_mut_windows<T>(block, windows, body)
//                   the frame door of some windows of a named block, as
//                   slice::get_disjoint_mut of Rust (1.86) gives one unique
//                   borrow for each of some indexes.  body gets one ViewMut<T>
//                   for each window, with the brand of the frame, and the door
//                   gives the result of body.  A window is the offset of its
//                   first byte and its count of bytes (ByteWindow).  The door
//                   checks each window in each build: inside the block, its
//                   first byte aligned for T, a whole count of T, and no byte
//                   shared with a second window.  A window comes from data
//                   outside the code, such as a memory plan, so a refused
//                   window gives a WindowRefusal and not an end of the process.
//                   A call takes at most max_block_windows windows, and the
//                   check of overlap compares each pair of them
//   with_mut_windows<T>(block, fixed_windows, body)
//                   the same door over a FixedArray of N windows, for a
//                   count that the caller knows at compile time.  body gets N
//                   arguments, one ViewMut<T> for each window, so no array of
//                   views lives in the frame.  The door tests each window
//                   and each pair at a fixed position, with no loop over a
//                   run-time count, and it gives the same refusals in the
//                   same order
// The block stays with the caller while body runs, so the door holds no
// proof that body reads no byte of the block through a second name.  The
// windows do not overlap, so two ViewMuts of one call never share a byte.
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
#include <foundation/core/List.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Side.h>

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

// The layout of the storage of a block from its two fields (THE STORAGE): the
// unaligned storage of the layout of the block, as the two doors of a block
// take it.  The fields come from a Layout, so the one check of the layout
// holds.  The check stays a call of its own, so a unit at its inline budget
// keeps the inline members of a block.  Complexity: O(1).
[[nodiscard]] constexpr ::foundation::alloc::Layout stored_block_layout(std::size_t bytes,
                                                                        std::size_t alignment) noexcept {
    return ::foundation::alloc::mint_layout(core::ByteCount{bytes}, ::foundation::alloc::Alignment{alignment})
        .expect("the fields of a block come from a layout")
        .unaligned_storage();
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

    // The storage of a block of the layout, from the door of the kind (THE
    // STORAGE).  The layout of the caller holds each fact, so its unaligned
    // storage needs no check.  The take and the release of a block are
    // always inline (foundation/core/Owner.h).  Complexity: one allocation.
    [[nodiscard]] CRUCIBLE_INLINE static AlignedBytesState allocated(::foundation::alloc::Layout layout,
                                                                     ::foundation::detail::BlockStorage kind) noexcept {
        ::foundation::alloc::Layout const stored = layout.unaligned_storage();
        void* const taken =
            kind == ::foundation::detail::BlockStorage::zeroed ? heap_allocate_zeroed_(stored) : heap_allocate_(stored);
        return AlignedBytesState{static_cast<std::uint8_t*>(taken), layout.bytes().raw(),
                                 static_cast<std::size_t>(layout.alignment())};
    }

    // Gives the storage back to the allocation door with the layout of the
    // storage, as the two doors of a block take it.  The release checks the
    // layout of its fields one time.
    CRUCIBLE_INLINE static constexpr void release(AlignedBytesState const& state) noexcept {
        if (state.storage != nullptr) {
            heap_release_(state.storage, ::foundation::detail::stored_block_layout(state.bytes, state.alignment));
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
    // wrote.  The constructor is always inline, as the constructor of an
    // UnwrittenBytes is.
    CRUCIBLE_INLINE AlignedBytes(detail::AlignedBytesKey const& /*key*/, State const& state) noexcept
        : core::OwnerCore<State>{state} {}

    AlignedBytes(AlignedBytes const&) =
        delete("an AlignedBytes owns its block alone: a copy would give it back two times");
    AlignedBytes& operator=(AlignedBytes const&) =
        delete("an AlignedBytes owns its block alone: a copy would give it back two times");

    // The assignment gives the block of this owner back after it takes the
    // block of other.  The moves and the end are always inline, as those of
    // the core are (foundation/core/Owner.h).
    CRUCIBLE_INLINE AlignedBytes(AlignedBytes&&) noexcept = default;
    CRUCIBLE_INLINE AlignedBytes& operator=(AlignedBytes&&) noexcept = default;
    CRUCIBLE_INLINE constexpr ~AlignedBytes() = default;

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

    // A block of the layout, with the key that only AlignedBytesDoor builds.
    // The bytes start their lifetime with no value and with no write.  The
    // constructor is always inline, as the moves are.  A constructor out of
    // line writes the three fields to memory one word at a time, and a caller
    // that reads two of them with one 16-byte load waits for a store forward
    // that cannot occur (THE COPY OF A STATE of foundation/core/Owner.h).
    // Complexity: one allocation.
    CRUCIBLE_INLINE UnwrittenBytes(detail::AlignedBytesKey const& /*key*/, ::foundation::alloc::Layout layout) noexcept
        : core::OwnerCore<State>{State::allocated(layout, detail::BlockStorage::unwritten)} {}

    UnwrittenBytes(UnwrittenBytes const&) =
        delete("an UnwrittenBytes owns its block alone: a copy would give it back two times");
    UnwrittenBytes& operator=(UnwrittenBytes const&) =
        delete("an UnwrittenBytes owns its block alone: a copy would give it back two times");
    // The moves and the end are always inline, as those of the core are.
    CRUCIBLE_INLINE UnwrittenBytes(UnwrittenBytes&&) noexcept = default;
    CRUCIBLE_INLINE UnwrittenBytes& operator=(UnwrittenBytes&&) noexcept = default;
    CRUCIBLE_INLINE constexpr ~UnwrittenBytes() = default;

    // The count of bytes that the mint asked for, without the round up.
    [[nodiscard]] std::size_t size() const noexcept { return state_.bytes; }

    // The alignment of the first byte, or zero for the empty block.
    [[nodiscard]] std::size_t alignment() const noexcept { return state_.alignment; }

    // Writes value into each byte, and gives the block as an AlignedBytes.
    // This block is then empty.  The local state of fill and of write is not
    // const, so it stays in registers (THE COPY OF A STATE of
    // foundation/core/Owner.h).  Complexity: O(size()).
    [[nodiscard]] AlignedBytes fill(std::uint8_t value) && noexcept {
        State given = take_state_();
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
        State given = take_state_();
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

// A block of the layout, in new storage of the allocation door, whose bytes
// hold no value.  The allocation capability comes first, so each allocation
// shows at its call.  The two mints are always inline, as the constructors
// are, so the fields of the block reach the caller in registers.
// Complexity: one allocation, and no write of a byte.
template <class Marker>
    requires CanMintAlignedBytes<Marker>
[[nodiscard]] CRUCIBLE_INLINE UnwrittenBytes mint_aligned_bytes(Marker /*allocation*/,
                                                                ::foundation::alloc::Layout layout) noexcept {
    return UnwrittenBytes{detail::AlignedBytesDoor::key(), layout};
}

// A block of the layout, in new storage of the zeroed allocation door, whose
// bytes each read zero.  Complexity: one allocation.  A large block comes
// from fresh pages of the kernel, so the mint writes no page of it, except
// under ThreadSanitizer (THE MINTS).
template <class Marker>
    requires CanMintAlignedBytes<Marker>
[[nodiscard]] CRUCIBLE_INLINE AlignedBytes mint_aligned_bytes_zeroed(Marker /*allocation*/,
                                                                     ::foundation::alloc::Layout layout) noexcept {
    return AlignedBytes{detail::AlignedBytesDoor::key(),
                        core::detail::owner::AlignedBytesState::allocated(layout, detail::BlockStorage::zeroed)};
}

// ── The windows of a block ─────────────────────────────────────────────────

namespace core::side {

// The two roles of the values of a ByteWindow: the offset of its first byte
// in its block, and its count of bytes.
struct WindowStart final {};
struct WindowLength final {};

}  // namespace core::side

// A window of a block: the offset of its first byte and its count of bytes.
// The two values have one type, so each comes in its role, and no call can
// swap them.
class ByteWindow final {
public:
    constexpr ByteWindow(core::Side<core::side::WindowStart, core::ByteCount> start,
                         core::Side<core::side::WindowLength, core::ByteCount> length) noexcept
        : start_{start.value()}, length_{length.value()} {}

    [[nodiscard]] constexpr core::ByteCount start() const noexcept { return start_; }
    [[nodiscard]] constexpr core::ByteCount length() const noexcept { return length_; }

private:
    core::ByteCount start_{std::size_t{0}};
    core::ByteCount length_{std::size_t{0}};
};

// Why with_mut_windows refused its windows.
enum class WindowRefusal : std::uint8_t {
    outside_block,  // a window ends past the last byte of the block
    misaligned,  // the first byte of a window is not aligned for the element
    partial_element,  // the count of bytes of a window holds no whole count of elements
    overlap,  // two windows share a byte
};

// The most windows of one call of with_mut_windows.  The check of overlap
// compares each pair of windows: at most 28 compares, which costs less than
// a sort for the outputs of one op.
inline constexpr std::size_t max_block_windows = 8;

namespace detail {

// The value of a Result of with_mut_windows: Unit for a body that gives
// void, and the result of the body for each other body.
template <class Given>
using window_frame_value_t = std::conditional_t<std::is_void_v<Given>, core::Unit, std::remove_cv_t<Given>>;

// True when the two windows share a byte.  A window of no byte shares none.
// Each window is inside its block, so no end wraps.
[[nodiscard]] constexpr bool windows_overlap_(ByteWindow const& first, ByteWindow const& second) noexcept {
    std::size_t const first_start = first.start().raw();
    std::size_t const second_start = second.start().raw();
    std::size_t const first_end = first_start + first.length().raw();
    std::size_t const second_end = second_start + second.length().raw();
    return first_start < first_end && second_start < second_end && first_start < second_end && second_start < first_end;
}

// The result of a body of a frame of the windows of the form of a list.
template <class Body, class T, class Fresh, std::size_t N>
using listed_body_result_t = std::invoke_result_t<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>;

// The windows of the form of a list, each tested, then each pair, and the
// views lent to body (THE WINDOWS).  The doors of a block and of a keyed block
// call it with the brand of their frame.  Complexity: O(W) for the windows and
// O(W^2) for the check of overlap, and the cost of the body.
template <class T, class Fresh, std::size_t N, class Body>
[[nodiscard]] core::Result<window_frame_value_t<listed_body_result_t<Body, T, Fresh, N>>, WindowRefusal>
lend_listed_windows_(AlignedBytes& block, core::FixedList<ByteWindow, N> const& windows, Body&& body) noexcept {
    using Views = core::FixedList<core::ViewMut<T, Fresh>, N>;
    using Given = listed_body_result_t<Body, T, Fresh, N>;
    core::View<std::uint8_t> const bytes = block.view();
    core::View<ByteWindow const> const asked = windows.view();
    std::uint8_t* const block_first = first_of_(core::detail::ViewDoor::key(), bytes);
    for (ByteWindow const& window : asked) {
        if (bytes.window(window.start().raw(), window.length().raw()).is_none()) [[unlikely]] {
            return core::err(WindowRefusal::outside_block);
        }
        if (window.length().raw() % sizeof(T) != 0) [[unlikely]] {
            return core::err(WindowRefusal::partial_element);
        }
        if ((__builtin_bit_cast(std::uintptr_t, block_first + window.start().raw()) & (alignof(T) - 1)) != 0)
            [[unlikely]] {
            return core::err(WindowRefusal::misaligned);
        }
    }
    std::size_t outer = 0;
    for (ByteWindow const& first_window : asked) {
        std::size_t inner = 0;
        for (ByteWindow const& second_window : asked) {
            if (inner > outer && windows_overlap_(first_window, second_window)) [[unlikely]] {
                return core::err(WindowRefusal::overlap);
            }
            ++inner;
        }
        ++outer;
    }
    // Each window starts the lifetime of its elements in a loop of its own.
    // The start is a barrier for the memory of the window, so a later loop
    // builds the views, and the count of the list stays in a register
    // between two appends.  The laundered address then names the elements
    // that the start began.
    for (ByteWindow const& window : asked) {
        static_cast<void>(lifetime::start_as_array<T>(
            bytes.window(window.start().raw(), window.length().raw()).expect("the test above found the window")));
    }
    Views views{};
    for (ByteWindow const& window : asked) {
        std::size_t const count = window.length().raw() / sizeof(T);
        T* const first = static_cast<T*>(static_cast<void*>(block_first + window.start().raw()));
        core::View<T> const elements{core::detail::ViewDoor::key(), count == 0 ? first : __builtin_launder(first),
                                     count};
        static_cast<void>(views.append(core::ViewMut<T, Fresh>{core::detail::FrameDoor::key(), elements})
                              .expect("the list of views holds one view for each window"));
    }
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(views);
        return core::Unit{};
    } else {
        return static_cast<Body&&>(body)(views);
    }
}

}  // namespace detail

// The frame door of some windows of a named block (THE WINDOWS).  body gets
// a reference to a FixedList of one ViewMut<T, Fresh> for each window, in the
// order of the windows, and the door gives its result, or Unit for a body
// that gives void.  A ViewMut does not copy, so a body that takes the list by
// value does not fit, and the list never moves out of the frame.
// A refused window gives its WindowRefusal, and body does not run.  Each
// window starts the lifetime of its elements of T over its bytes
// (start_as_array of foundation/core/Array.h), so T is an implicit-lifetime
// type throughout.  The pack refuses each explicit argument after T, so no
// caller names the brand of the frame.  Complexity: O(W) for the windows and
// O(W^2) for the check of overlap, with W at most max_block_windows, and the
// cost of the body.
template <class T, class... Refused, std::size_t N, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (N <= max_block_windows)
         && core::UniqueTarget<T> && lifetime::ImplicitLifetimeThroughout<T>
         && std::is_nothrow_invocable_v<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>
         && core::FrameResult<std::invoke_result_t<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>, Fresh>
[[nodiscard]] core::Result<
    detail::window_frame_value_t<std::invoke_result_t<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>>,
    WindowRefusal>
with_mut_windows(AlignedBytes& block, core::FixedList<ByteWindow, N> const& windows, Body&& body) noexcept {
    return detail::lend_listed_windows_<T, Fresh>(block, windows, static_cast<Body&&>(body));
}

// A body whose result holds a borrow or names the brand of the frame binds
// to this overload, so the refusal gives its reason.
template <class T, class... Refused, std::size_t N, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (N <= max_block_windows) && core::UniqueTarget<T>
             && std::is_nothrow_invocable_v<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>
             && (!core::FrameResult<std::invoke_result_t<Body &&, core::FixedList<core::ViewMut<T, Fresh>, N>&>, Fresh>)
void with_mut_windows(AlignedBytes& block, core::FixedList<ByteWindow, N> const& windows, Body&& body) = delete(
    "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
    "the frame: give a plain value");

// More windows than the check of overlap compares bind to this overload.
template <class T, class... Refused, std::size_t N, class Body>
    requires(N > max_block_windows)
void with_mut_windows(AlignedBytes& block, core::FixedList<ByteWindow, N> const& windows, Body&& body) =
    delete("with_mut_windows takes at most max_block_windows windows, because its check of overlap compares each pair");

// A temporary block gives its bytes back at the end of the full expression.
template <class T, class... Refused, std::size_t N, class Body>
void with_mut_windows(AlignedBytes&& block, core::FixedList<ByteWindow, N> const& windows, Body&& body) =
    delete("a ViewMut of a temporary block points into bytes that the full expression gives back: name the block");

// ── The windows of a static count ──────────────────────────────────────────

namespace detail {

// The tests of one window, in the order of the form of a list: inside the
// block, a whole count of T, and the first byte aligned for T.  A refused
// window writes its WindowRefusal to refusal and gives true.  Complexity:
// O(1).
template <class T>
[[nodiscard]] CRUCIBLE_INLINE bool is_window_refused_(core::View<std::uint8_t> bytes, ByteWindow const& window,
                                                      WindowRefusal& refusal) noexcept {
    std::uint8_t* const block_first = first_of_(core::detail::ViewDoor::key(), bytes);
    std::size_t const start = window.start().raw();
    // The two compares of the bound of a window, with no Option of a View.
    if (start > bytes.size() || window.length().raw() > bytes.size() - start) [[unlikely]] {
        refusal = WindowRefusal::outside_block;
        return true;
    }
    if (window.length().raw() % sizeof(T) != 0) [[unlikely]] {
        refusal = WindowRefusal::partial_element;
        return true;
    }
    if ((__builtin_bit_cast(std::uintptr_t, block_first + start) & (alignof(T) - 1)) != 0) [[unlikely]] {
        refusal = WindowRefusal::misaligned;
        return true;
    }
    return false;
}

// True when the window at First shares a byte with a window after it.  Each
// term with a position at or before First is false at compile time, so the
// compiler emits only the pairs after First.  Complexity: O(N).
template <std::size_t First, std::size_t N, std::size_t... Seconds>
[[nodiscard]] CRUCIBLE_INLINE constexpr bool
overlaps_a_later_window_(core::FixedArray<ByteWindow, N> const& windows,
                         std::index_sequence<Seconds...> /*seconds*/) noexcept {
    return (
        false || ...
        || (Seconds > First && windows_overlap_(windows.at(core::index<First>()), windows.at(core::index<Seconds>()))));
}

// True when two windows of the array share a byte: each pair, one time.
// Complexity: O(N^2) compares, all at fixed positions.
template <std::size_t N, std::size_t... Firsts>
[[nodiscard]] CRUCIBLE_INLINE constexpr bool has_window_overlap_(core::FixedArray<ByteWindow, N> const& windows,
                                                                 std::index_sequence<Firsts...> /*firsts*/) noexcept {
    return (false || ... || overlaps_a_later_window_<Firsts>(windows, std::make_index_sequence<N>{}));
}

// A window of a block that passed the tests of one window for T: it is
// inside the bytes of the block, its bytes hold a whole count of T, and its
// first byte is aligned for T.  test is its one door, so a tested window
// holds the three facts that a start of the lifetime of its elements needs:
//   - The first byte is aligned for T, which the test of the alignment gives.
//   - The count of T is at most max_view_count(sizeof(T)), because a View of
//     the block holds at most max_view_count(1) bytes.
//   - The first byte is not null when the window holds an element, because
//     a View holds a pointer that is not null for each count above zero.
// So lend starts the elements through the form of start_as_array with the
// key of a View, which checks nothing (foundation/core/Array.h).  The window
// of no element at no address also holds the three facts, so it is the
// default.  The type holds the first byte and the count, so it costs what
// the two words cost.
template <class T>
class TestedWindow final {
public:
    constexpr TestedWindow() noexcept = default;

    // The tests of is_window_refused_, in its order.  A window that passes
    // them gives false and writes its tested window to tested.  A refused
    // window gives true, writes its WindowRefusal to refusal, and leaves
    // tested as it was.  The two words of a tested window then stay in
    // registers, where a Result of a tested window took a slot of the
    // frame at -O1.  Complexity: O(1).
    [[nodiscard]] CRUCIBLE_INLINE static bool is_refused(core::View<std::uint8_t> bytes, ByteWindow const& window,
                                                         TestedWindow& tested, WindowRefusal& refusal) noexcept {
        if (is_window_refused_<T>(bytes, window, refusal)) [[unlikely]] {
            return true;
        }
        tested = TestedWindow{first_of_(core::detail::ViewDoor::key(), bytes) + window.start().raw(),
                              window.length().raw() / sizeof(T)};
        return false;
    }

    // The ViewMut of the elements of the window, with the brand of the frame.
    // The start of their lifetime gives their View, with their values.  A
    // window of no element starts no lifetime, so its View holds no object.
    // Complexity: O(1).
    template <::foundation::brand::IsBrand Fresh>
    [[nodiscard]] CRUCIBLE_INLINE core::ViewMut<T, Fresh> lend() const noexcept {
        return core::ViewMut<T, Fresh>{core::detail::FrameDoor::key(),
                                       lifetime::start_as_array<T>(core::detail::ViewDoor::key(), first_, count_)};
    }

private:
    constexpr TestedWindow(std::uint8_t* first, std::size_t count) noexcept : first_{first}, count_{count} {}

    std::uint8_t* first_ = nullptr;
    std::size_t count_ = 0;
};

// The type of the argument of a body of the static form at a position: one
// ViewMut for each window.
template <class View, std::size_t Position>
using window_argument_t = View;

// True when body takes one View for each of N positions, with no exception.
// The gates of the door read only a concept and an alias, so no unit can
// specialize a template that changes what they admit.
template <class Body, class View, std::size_t N>
concept WindowBody = []<std::size_t... Positions>(std::index_sequence<Positions...> /*positions*/) consteval {
    return std::is_nothrow_invocable_v<Body, window_argument_t<View, Positions>...>;
}(std::make_index_sequence<N>{});

// The result of a body that takes one View for each of N positions.
template <class Body, class View, std::size_t N>
using window_body_result_t =
    typename decltype([]<std::size_t... Positions>(std::index_sequence<Positions...> /*positions*/) consteval {
        return std::type_identity<std::invoke_result_t<Body, window_argument_t<View, Positions>...>>{};
    }(std::make_index_sequence<N>{}))::type;

// The windows from Position on, each tested in order, then each pair, and
// the tested windows lent to body (the door below).  tested holds the
// windows before Position, which passed their tests.  A tested window goes
// by value, so the door reads each of its two words one time, before the
// start of the lifetime of a window hides the memory of the window from the
// optimizer.  Complexity: N - Position tests, and the compares of the pairs.
template <class T, ::foundation::brand::IsBrand Fresh, std::size_t Position, std::size_t N, class Body, class... Tested>
[[nodiscard]] CRUCIBLE_INLINE auto lend_tested_windows_(core::View<std::uint8_t> bytes,
                                                        core::FixedArray<ByteWindow, N> const& windows, Body&& body,
                                                        Tested... tested) noexcept
    -> core::Result<window_frame_value_t<window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>>, WindowRefusal> {
    if constexpr (Position < N) {
        TestedWindow<T> next{};
        WindowRefusal refusal = WindowRefusal::overlap;
        if (TestedWindow<T>::is_refused(bytes, windows.at(core::index<Position>()), next, refusal)) [[unlikely]] {
            return core::err(refusal);
        }
        return lend_tested_windows_<T, Fresh, Position + 1>(bytes, windows, static_cast<Body&&>(body), tested..., next);
    } else {
        if (has_window_overlap_(windows, std::make_index_sequence<N>{})) [[unlikely]] {
            return core::err(WindowRefusal::overlap);
        }
        // Each ViewMut goes to body as an argument of its own, so no array of
        // views lives in the frame.  The windows do not overlap, so the order
        // of the starts of their lifetimes does not change a byte.
        using Given = window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(tested.template lend<Fresh>()...);
            return core::Unit{};
        } else {
            return static_cast<Body&&>(body)(tested.template lend<Fresh>()...);
        }
    }
}

}  // namespace detail

// The frame door of a static count of windows of a named block (THE
// WINDOWS).  The count N is a template parameter, and the windows are a
// FixedArray of N, so the door tests each window and each pair at a fixed
// position, with no loop over a run-time count.  body gets N arguments, one
// ViewMut<T, Fresh> for each window, in the order of the windows, so it
// takes each by value or by a forwarding reference.  No array of views
// lives in the frame.  The door gives the result of body, or Unit for a body
// that gives void.  The tests and the refusals are those of the form of a
// list, in the same order: the tests of each window in order, then the
// pairs.  Complexity: N tests and N(N-1)/2 compares, with N at most
// max_block_windows, and the cost of the body.
template <class T, class... Refused, std::size_t N, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (N <= max_block_windows) && core::UniqueTarget<T>
         && lifetime::ImplicitLifetimeThroughout<T> && detail::WindowBody<Body&&, core::ViewMut<T, Fresh>, N>
         && core::FrameResult<detail::window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>, Fresh>
[[nodiscard]] core::Result<
    detail::window_frame_value_t<detail::window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>>, WindowRefusal>
with_mut_windows(AlignedBytes& block, core::FixedArray<ByteWindow, N> const& windows, Body&& body) noexcept {
    return detail::lend_tested_windows_<T, Fresh, 0>(block.view(), windows, static_cast<Body&&>(body));
}

// A body whose result holds a borrow or names the brand of the frame binds
// to this overload, so the refusal gives its reason.
template <class T, class... Refused, std::size_t N, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (N <= max_block_windows)
             && core::UniqueTarget<T> && detail::WindowBody<Body&&, core::ViewMut<T, Fresh>, N>
             && (!core::FrameResult<detail::window_body_result_t<Body &&, core::ViewMut<T, Fresh>, N>, Fresh>)
void with_mut_windows(AlignedBytes& block, core::FixedArray<ByteWindow, N> const& windows, Body&& body) = delete(
    "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
    "the frame: give a plain value");

// More windows than the check of overlap compares bind to this overload.
template <class T, class... Refused, std::size_t N, class Body>
    requires(N > max_block_windows)
void with_mut_windows(AlignedBytes& block, core::FixedArray<ByteWindow, N> const& windows, Body&& body) =
    delete("with_mut_windows takes at most max_block_windows windows, because its check of overlap compares each pair");

// A temporary block gives its bytes back at the end of the full expression.
template <class T, class... Refused, std::size_t N, class Body>
void with_mut_windows(AlignedBytes&& block, core::FixedArray<ByteWindow, N> const& windows, Body&& body) =
    delete("a ViewMut of a temporary block points into bytes that the full expression gives back: name the block");

// ── The keyed block ────────────────────────────────────────────────────────

// A block that opens the frames of its windows only with its LoanKey (THE
// LOAN KEY of foundation/core/Frame.h).  It holds an AlignedBytes by value, so
// a move takes three words.  It reads its bytes through view() const&, and it
// gives no View that writes them, so a body of a frame of its windows writes
// the bytes only through the ViewMuts of the frame.
template <class Brand>
    requires core::IsKeyBrand<Brand>
class [[nodiscard]][[= core::loan_key_mark{}]] KeyedBytes final {
    AlignedBytes bytes_{};

public:
    // The block of the bytes, with the key that only AlignedBytesDoor builds.
    KeyedBytes(detail::AlignedBytesKey const& /*key*/, AlignedBytes&& bytes) noexcept : bytes_{std::move(bytes)} {}

    KeyedBytes(KeyedBytes const&) = delete("a KeyedBytes owns its block alone: a copy would give it back two times");
    KeyedBytes&
    operator=(KeyedBytes const&) = delete("a KeyedBytes owns its block alone: a copy would give it back two times");
    KeyedBytes(KeyedBytes&&) noexcept = default;
    KeyedBytes& operator=(KeyedBytes&&) noexcept = default;
    ~KeyedBytes() = default;

    // The count of bytes that the mint of the block asked for, and the
    // alignment of its first byte.
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t alignment() const noexcept { return bytes_.alignment(); }

    // The View of the const bytes.  It borrows this block.
    [[nodiscard]] core::View<std::uint8_t const> view() const& noexcept { return bytes_.view(); }
    void view() const&& = delete("a view of a temporary KeyedBytes points into a block that the full expression ends");

    // The block for the keyed window doors.  The key of a frame keeps each
    // caller outside the base out.
    [[nodiscard]] friend AlignedBytes& keyed_block_of_(core::detail::FrameKey const& /*key*/,
                                                       KeyedBytes& block) noexcept {
        return block.bytes_;
    }
};

// The gate of mint_keyed_bytes: an AlignedBytes that the caller gives up, and
// no explicit argument.
template <class Block, class... Refused>
concept CanMintKeyedBytes = (sizeof...(Refused) == 0) && std::is_same_v<Block, AlignedBytes>;

// A keyed block of the bytes of block, and its LoanKey: auto [keyed, key] =
// mint_keyed_bytes(std::move(block)).  The brand is the fresh brand of the
// call site, in undeducible_brand_t.  Complexity: O(1), a move of three words.
template <class... Refused, class Block, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires CanMintKeyedBytes<Block, Refused...>
[[nodiscard]] core::Record<KeyedBytes<::foundation::brand::undeducible_brand_t<Fresh>>,
                           core::LoanKey<::foundation::brand::undeducible_brand_t<Fresh>>>
mint_keyed_bytes(Block&& block) noexcept {
    return {KeyedBytes<Fresh>{detail::AlignedBytesDoor::key(), static_cast<Block&&>(block)},
            core::LoanKey<Fresh>{core::detail::LoanKeyDoor::token()}};
}

// A named block stays with the caller, so the keyed block would share its
// bytes with a second name.
template <class... Refused, class Block>
    requires(std::is_lvalue_reference_v<Block> || std::is_const_v<std::remove_reference_t<Block>>)
             && std::is_same_v<std::remove_cvref_t<Block>, AlignedBytes>
void mint_keyed_bytes(Block&& block) =
    delete("mint_keyed_bytes takes the block into the keyed block: pass std::move(block)");

namespace detail {

// The body of a keyed frame of the windows of the form of a list: the body
// of the door of a block, which reaches no key of the brand (THE GATE).
template <class Body, class T, class Brand, class Fresh, std::size_t N>
concept KeyedListedWindowBody =
    std::is_nothrow_invocable_v<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>
    && core::FrameResult<listed_body_result_t<Body, T, Fresh, N>, Fresh> && core::ReachesNoLoanKey<Body, Brand>;

// The body of a keyed frame of a static count of windows.
template <class Body, class T, class Brand, class Fresh, std::size_t N>
concept KeyedFixedWindowBody = WindowBody<Body&&, core::ViewMut<T, Fresh>, N>
                            && core::FrameResult<window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>, Fresh>
                            && core::ReachesNoLoanKey<Body, Brand>;

// The element of the windows of a keyed frame.
template <class T, std::size_t N>
concept KeyedWindowElement =
    (N <= max_block_windows) && core::UniqueTarget<T> && lifetime::ImplicitLifetimeThroughout<T>;

}  // namespace detail

// The keyed frame door of some windows of a keyed block (THE WINDOWS, THE
// LOAN KEY).  It gives the views as with_mut_windows of an AlignedBytes does,
// and its tests and refusals are those of that door.  The key moves into the
// door, and the door gives it back with the Result, also after a refused
// window.  The body reaches no key of the brand of the block (THE GATE), so
// no code inside the frame opens a second frame of the windows of the block.
// Complexity: as with_mut_windows of an AlignedBytes.
template <class T, class... Refused, std::size_t N, class Brand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
         && detail::KeyedWindowElement<T, N> && detail::KeyedListedWindowBody<Body, T, Brand, Fresh, N>
[[nodiscard]] core::Record<
    core::Result<detail::window_frame_value_t<detail::listed_body_result_t<Body, T, Fresh, N>>, WindowRefusal>,
    core::LoanKey<Brand>>
with_mut_windows(KeyedBytes<Brand>& block, core::LoanKey<Brand>&& key, core::FixedList<ByteWindow, N> const& windows,
                 Body&& body) noexcept {
    return {detail::lend_listed_windows_<T, Fresh>(keyed_block_of_(core::detail::FrameDoor::key(), block), windows,
                                                   static_cast<Body&&>(body)),
            std::move(key)};
}

// The same door with a borrow of the key for the call: the key stays with the
// caller, and the door gives the Result.
template <class T, class... Refused, std::size_t N, class Brand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
         && detail::KeyedWindowElement<T, N> && detail::KeyedListedWindowBody<Body, T, Brand, Fresh, N>
[[nodiscard]] core::Result<detail::window_frame_value_t<detail::listed_body_result_t<Body, T, Fresh, N>>, WindowRefusal>
with_mut_windows(KeyedBytes<Brand>& block, core::LoanKey<Brand>& /*key*/, core::FixedList<ByteWindow, N> const& windows,
                 Body&& body) noexcept {
    return detail::lend_listed_windows_<T, Fresh>(keyed_block_of_(core::detail::FrameDoor::key(), block), windows,
                                                  static_cast<Body&&>(body));
}

// The keyed frame door of a static count of windows (THE WINDOWS, THE LOAN
// KEY): the door of the static count of an AlignedBytes, with the key moved in
// and given back.
template <class T, class... Refused, std::size_t N, class Brand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
         && detail::KeyedWindowElement<T, N> && detail::KeyedFixedWindowBody<Body, T, Brand, Fresh, N>
[[nodiscard]] core::Record<
    core::Result<detail::window_frame_value_t<detail::window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>>,
                 WindowRefusal>,
    core::LoanKey<Brand>>
with_mut_windows(KeyedBytes<Brand>& block, core::LoanKey<Brand>&& key, core::FixedArray<ByteWindow, N> const& windows,
                 Body&& body) noexcept {
    return {detail::lend_tested_windows_<T, Fresh, 0>(keyed_block_of_(core::detail::FrameDoor::key(), block).view(),
                                                      windows, static_cast<Body&&>(body)),
            std::move(key)};
}

// The same door with a borrow of the key for the call.
template <class T, class... Refused, std::size_t N, class Brand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0)
         && detail::KeyedWindowElement<T, N> && detail::KeyedFixedWindowBody<Body, T, Brand, Fresh, N>
[[nodiscard]] core::Result<
    detail::window_frame_value_t<detail::window_body_result_t<Body&&, core::ViewMut<T, Fresh>, N>>, WindowRefusal>
with_mut_windows(KeyedBytes<Brand>& block, core::LoanKey<Brand>& /*key*/,
                 core::FixedArray<ByteWindow, N> const& windows, Body&& body) noexcept {
    return detail::lend_tested_windows_<T, Fresh, 0>(keyed_block_of_(core::detail::FrameDoor::key(), block).view(),
                                                     windows, static_cast<Body&&>(body));
}

// A body of a keyed frame of windows that reaches a key of the brand of the
// block binds to these overloads, so the refusal gives its reason.
template <class T, class... Refused, std::size_t N, class Brand, class Key, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && std::is_same_v<std::remove_cvref_t<Key>, core::LoanKey<Brand>>
             && std::is_nothrow_invocable_v<Body&&, core::FixedList<core::ViewMut<T, Fresh>, N>&>
             && (!core::ReachesNoLoanKey<Body, Brand>)
void with_mut_windows(KeyedBytes<Brand>& block, Key&& key, core::FixedList<ByteWindow, N> const& windows, Body&& body) =
    delete("a body of a keyed frame reaches a LoanKey of the brand of its block, so code in the body could open a "
           "second frame of the windows: capture only what the body reads, and keep the key with the caller");
template <class T, class... Refused, std::size_t N, class Brand, class Key, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && std::is_same_v<std::remove_cvref_t<Key>, core::LoanKey<Brand>>
             && detail::WindowBody<Body&&, core::ViewMut<T, Fresh>, N> && (!core::ReachesNoLoanKey<Body, Brand>)
void with_mut_windows(KeyedBytes<Brand>& block, Key&& key, core::FixedArray<ByteWindow, N> const& windows,
                      Body&& body) =
    delete("a body of a keyed frame reaches a LoanKey of the brand of its block, so code in the body could open a "
           "second frame of the windows: capture only what the body reads, and keep the key with the caller");

}  // namespace foundation
