#pragma once

// The layout of an allocation: a count of bytes and an alignment, as one
// checked value (THE LAYOUT).
//
//   Alignment                      a count of bytes that is a power of two
//   alignment_of<T>()              the alignment of the type T
//   mint_alignment(bytes)          the Alignment of bytes, or the error
//                                  alignment_not_power_of_two
//   bytes_of(alignment)            the bytes of an Alignment
//   Layout                         a count of bytes above zero at an
//                                  Alignment
//   Layout::of<T>()                the layout of one object of T
//   Layout::of_array<T>(count)     the layout of count objects of T, or the
//                                  error zero_bytes or too_large
//   mint_layout(bytes, alignment)  the layout of bytes bytes at the
//                                  alignment, or an error of LayoutError
//   layout.bytes(), layout.alignment()
//
//   SizeClass                      one of the 64 slot sizes of the allocator
//                                  of the base, 1 to 64 (THE SIZE CLASSES)
//   slot_bytes(size_class)         the bytes of one slot of the class
//   class_for(layout)              the smallest class whose slot holds the
//                                  bytes and is a whole count of the
//                                  alignment, or none past 2 MiB
//   slab_kind_of(size_class)       the kind of the slab of the class
//   slab_slots(size_class)         the slots of one slab of the class
//   bin_capacity(size_class)       the slots that the bin of a thread holds
//
// THE LAYOUT
//   The allocation door of foundation/core/Heap.h takes one Layout.  So a
//   count of bytes and an alignment cannot swap at a call, and no call gives
//   the door a request that it refuses.  A Layout holds these facts:
//     - The bytes are more than zero.
//     - The alignment is a power of two.
//     - The bytes, rounded up to a whole count of the alignment, are at most
//       kMaxLayoutBytes, the largest object that the allocator of the C
//       library gives.
//   Only the three doors of Layout make one, and each door checks the facts.
//   Layout::of<T>() needs no check at run time: each complete object type
//   has a size above zero, and its alignment is a power of two.  The copy of
//   a Layout is user-provided, so std::bit_cast builds no Layout from bytes
//   (test/fixy/test_forgeable_proofs.cpp).
//
// THE ALIGNMENT
//   Alignment is a scoped enumeration over std::size_t.  It passes in a
//   register, and no number converts to it.  Its value names a power of two
//   by its contract, but a cast can name each number.  So mint_layout tests
//   the power again, and only a Layout holds the fact.
//
// THE SIZE CLASSES
//   Classes 1 to 8 hold 16 to 128 bytes in steps of 16.  Above 128 bytes each
//   doubling has four classes, at 5/4, 6/4, 7/4 and 2 times the doubling
//   below, to 2 MiB in class 64.  So a slot above 128 bytes holds less than a
//   quarter more than the smallest request of its class, the bound of the
//   classes of jemalloc.  Each slot is a whole count of 16 bytes, and the last
//   class of each doubling is a power of two.
//
//   The class of bytes takes one compare to choose between two formulas, and
//   each formula has no branch:
//     to 128 bytes:  (bytes + 15) >> 4
//     above:         last = bytes - 1, top = the index of the highest bit of
//                    last, class = (top - 5) * 4 + ((last >> (top - 2)) & 3) + 1
//   class_for adds one compare of the alignment.  An alignment above 16 steps
//   up at most three classes, because the last class of a doubling is a power
//   of two, and that power is a whole count of each smaller power of two.  A
//   constant layout folds its class at compile time.  Each compare of the
//   run-time path is one test, because -fharden-compares doubles a compound
//   condition.
//
//   The slab of a class:
//     small   one unit of 64 KiB, classes 1 to 32 (slots to 8 KiB)
//     medium  one chunk of 2 MiB, classes 33 to 52 (slots to 256 KiB)
//     large   a run of units of 64 KiB that holds one slot, classes 53 to 64
//   The bin of a thread holds 128 slots of a class to 128 bytes, then the
//   slots of 16 KiB, and at least two slots.

#include <foundation/contracts/Pre.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::alloc {

// The largest count of bytes of a Layout: the largest object that the
// allocator of the C library gives, which is the maximum of std::ptrdiff_t.
inline constexpr std::size_t kMaxLayoutBytes = static_cast<std::size_t>(__PTRDIFF_MAX__);

// A count of bytes that is a power of two (THE ALIGNMENT).
enum class Alignment : std::size_t {
};

// The reason that a door of Layout refuses a request.
enum class LayoutError : std::uint8_t {
    zero_bytes,
    alignment_not_power_of_two,
    too_large,
};

// A type that has a layout: a complete object type.  A reference, a
// function, void and an incomplete type have none.
template <class T>
concept LayoutType = std::is_object_v<T> && requires { sizeof(T); };

template <LayoutType T>
[[nodiscard]] consteval Alignment alignment_of() noexcept {
    return Alignment{alignof(T)};
}

// The Alignment of bytes, which must be a power of two.
[[nodiscard]] constexpr ::foundation::core::Result<Alignment, LayoutError>
mint_alignment(::foundation::core::ByteCount bytes) noexcept {
    if (!::foundation::core::has_single_bit(bytes.raw())) {
        return ::foundation::core::err(LayoutError::alignment_not_power_of_two);
    }
    return Alignment{bytes.raw()};
}

[[nodiscard]] constexpr ::foundation::core::ByteCount bytes_of(Alignment alignment) noexcept {
    return ::foundation::core::ByteCount{static_cast<std::size_t>(alignment)};
}

// A count of bytes above zero at an Alignment (THE LAYOUT).
//
// A Result of a Layout names a complete Layout, so the two doors that give
// one deduce their return type, and their bodies come after the class.
class Layout {
public:
    template <LayoutType T>
    [[nodiscard]] static consteval Layout of() noexcept {
        return Layout{::foundation::core::ByteCount{sizeof(T)}, alignment_of<T>()};
    }

    // The layout of count objects of T, a Result<Layout, LayoutError>.  Zero
    // objects have no bytes, and a count whose bytes pass kMaxLayoutBytes is
    // too large.
    template <LayoutType T>
    [[nodiscard]] static constexpr auto of_array(::foundation::core::ElementCount count) noexcept;

    constexpr Layout(Layout const& other) noexcept : bytes_{other.bytes_}, alignment_{other.alignment_} {}
    constexpr Layout& operator=(Layout const& other) noexcept {
        bytes_ = other.bytes_;
        alignment_ = other.alignment_;
        return *this;
    }
    ~Layout() = default;

    [[nodiscard]] constexpr ::foundation::core::ByteCount bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr Alignment alignment() const noexcept { return alignment_; }

    [[nodiscard]] constexpr bool operator==(Layout const& other) const noexcept {
        return bytes_ == other.bytes_ && alignment_ == other.alignment_;
    }

private:
    constexpr Layout(::foundation::core::ByteCount bytes, Alignment alignment) noexcept
        : bytes_{bytes}, alignment_{alignment} {}

    friend constexpr auto mint_layout(::foundation::core::ByteCount bytes, Alignment alignment) noexcept;

    ::foundation::core::ByteCount bytes_{std::size_t{0}};
    Alignment alignment_{};
};

// The result of each door of Layout that checks a request.
using LayoutResult = ::foundation::core::Result<Layout, LayoutError>;

template <LayoutType T>
constexpr auto Layout::of_array(::foundation::core::ElementCount count) noexcept {
    if (count.raw() == 0) return LayoutResult{::foundation::core::err(LayoutError::zero_bytes)};
    if (count.raw() > kMaxLayoutBytes / sizeof(T)) return LayoutResult{::foundation::core::err(LayoutError::too_large)};
    return LayoutResult{Layout{::foundation::core::ByteCount{count.raw() * sizeof(T)}, alignment_of<T>()}};
}

// The layout of bytes bytes at the alignment, a LayoutResult: the layout, or
// the first fact of THE LAYOUT that the request breaks.
[[nodiscard]] constexpr auto mint_layout(::foundation::core::ByteCount bytes, Alignment alignment) noexcept {
    auto const step = static_cast<std::size_t>(alignment);
    if (!::foundation::core::has_single_bit(step)) {
        return LayoutResult{::foundation::core::err(LayoutError::alignment_not_power_of_two)};
    }
    if (bytes.raw() == 0) return LayoutResult{::foundation::core::err(LayoutError::zero_bytes)};
    // The bytes are above zero, so a round-up that fits gives more than zero,
    // and zero marks a round-up past std::size_t.
    std::size_t const rounded = ::foundation::core::align_up(bytes.raw(), step).value_or(0);
    if (rounded == 0 || rounded > kMaxLayoutBytes) return LayoutResult{::foundation::core::err(LayoutError::too_large)};
    return LayoutResult{Layout{bytes, alignment}};
}

// ── The size classes ───────────────────────────────────────────────────

// One of the 64 slot sizes of the allocator of the base, 1 to 64 (THE SIZE
// CLASSES).  A value outside 1 to 64 names no class, and each query below
// refuses it.
enum class SizeClass : std::uint8_t {
};

inline constexpr std::uint8_t kSizeClassCount = 64;

// The bytes of the classes 1 to 8, in steps of kSlotStepBytes.
inline constexpr std::size_t kSlotStepBytes = 16;
inline constexpr std::size_t kSmallClassBytes = 128;

// The bytes of the largest class, 64.  A larger request has no class.
inline constexpr std::size_t kLargestSlotBytes = std::size_t{2} << 20;

// The unit of a small or a large slab, and the chunk of a medium slab.
inline constexpr std::size_t kSlabUnitBytes = std::size_t{64} << 10;
inline constexpr std::size_t kSlabChunkBytes = std::size_t{2} << 20;

// The last class of each slab kind.
inline constexpr std::uint8_t kLastSmallClass = 32;
inline constexpr std::uint8_t kLastMediumClass = 52;

// The slots of a bin of a class to 128 bytes, the bytes of the slots of the
// bin of a larger class, and the fewest slots of a bin.
inline constexpr std::uint32_t kSmallBinSlots = 128;
inline constexpr std::size_t kBinBytes = std::size_t{16} << 10;
inline constexpr std::uint32_t kFewestBinSlots = 2;

// The kind of the slab of a class (THE SIZE CLASSES).
enum class SlabKind : std::uint8_t {
    small,
    medium,
    large,
};

// True when the value names a class, 1 to 64.
[[nodiscard]] constexpr bool is_size_class(SizeClass size_class) noexcept {
    auto const value = static_cast<std::uint8_t>(size_class);
    return value - 1u < kSizeClassCount;
}

// The bytes of one slot of the class.  Complexity: O(1), one compare.
[[nodiscard]] constexpr ::foundation::core::ByteCount slot_bytes(SizeClass size_class) noexcept {
    CRUCIBLE_PRE(is_size_class(size_class));
    std::size_t const value = static_cast<std::uint8_t>(size_class);
    if (value <= kSmallClassBytes / kSlotStepBytes) return ::foundation::core::ByteCount{value * kSlotStepBytes};
    std::size_t const above = value - kSmallClassBytes / kSlotStepBytes - 1;
    return ::foundation::core::ByteCount{((kSmallClassBytes << (above >> 2)) * (5 + (above & 3))) >> 2};
}

namespace detail {

// The smallest class whose slot holds bytes bytes, which are above zero and
// at most kLargestSlotBytes (THE SIZE CLASSES).  Complexity: O(1), one
// compare.
[[nodiscard]] constexpr SizeClass class_of_bytes_(std::size_t bytes) noexcept {
    if (bytes <= kSmallClassBytes) return SizeClass{static_cast<std::uint8_t>((bytes + kSlotStepBytes - 1) >> 4)};
    std::size_t const last = bytes - 1;
    auto const top = static_cast<std::size_t>(63 - ::foundation::core::countl_zero(last));
    return SizeClass{static_cast<std::uint8_t>((top - 5) * 4 + ((last >> (top - 2)) & 3) + 1)};
}

}  // namespace detail

// The smallest class whose slot holds the bytes of the layout and is a whole
// count of its alignment, or none for a layout past kLargestSlotBytes or for
// an alignment past it (THE SIZE CLASSES).  Complexity: O(1), at most four
// steps for an alignment above 16.
[[nodiscard]] constexpr ::foundation::core::Option<SizeClass> class_for(Layout const& layout) noexcept {
    std::size_t const bytes = layout.bytes().raw();
    auto const alignment = static_cast<std::size_t>(layout.alignment());
    if (alignment <= kSlotStepBytes) {
        if (bytes > kLargestSlotBytes) return ::foundation::core::none;
        return ::foundation::core::Option<SizeClass>::some(detail::class_of_bytes_(bytes));
    }
    std::size_t const wanted = bytes < alignment ? alignment : bytes;
    if (wanted > kLargestSlotBytes) return ::foundation::core::none;
    auto value = static_cast<std::uint8_t>(detail::class_of_bytes_(wanted));
    while (slot_bytes(SizeClass{value}).raw() % alignment != 0) {
        ++value;
    }
    return ::foundation::core::Option<SizeClass>::some(SizeClass{value});
}

[[nodiscard]] constexpr SlabKind slab_kind_of(SizeClass size_class) noexcept {
    CRUCIBLE_PRE(is_size_class(size_class));
    auto const value = static_cast<std::uint8_t>(size_class);
    if (value <= kLastSmallClass) return SlabKind::small;
    if (value <= kLastMediumClass) return SlabKind::medium;
    return SlabKind::large;
}

// The slots of one slab of the class: the slots of a unit, of a chunk, or one
// slot of a run of units.  Complexity: O(1), one division.
[[nodiscard]] constexpr std::uint32_t slab_slots(SizeClass size_class) noexcept {
    std::size_t const slot = slot_bytes(size_class).raw();
    switch (slab_kind_of(size_class)) {
        case SlabKind::small:
            return static_cast<std::uint32_t>(kSlabUnitBytes / slot);
        case SlabKind::medium:
            return static_cast<std::uint32_t>(kSlabChunkBytes / slot);
        case SlabKind::large:
            return 1;
        default:
            break;
    }
    ::foundation::core::unreachable();
}

// The slots that the bin of a thread holds for the class (THE SIZE CLASSES).
// Complexity: O(1), one division.
[[nodiscard]] constexpr std::uint32_t bin_capacity(SizeClass size_class) noexcept {
    std::size_t const slot = slot_bytes(size_class).raw();
    if (slot <= kSmallClassBytes) return kSmallBinSlots;
    std::size_t const slots = kBinBytes / slot;
    if (slots < kFewestBinSlots) return kFewestBinSlots;
    return static_cast<std::uint32_t>(slots);
}

}  // namespace foundation::alloc
