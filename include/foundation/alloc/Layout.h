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

}  // namespace foundation::alloc
