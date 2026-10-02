#pragma once

// The Region family: one contiguous run of elements, and its views.
//
// View<T> borrows a run of elements: a pointer and a count.  View<T, N>
// borrows a run of exactly N elements: a pointer alone, because the type
// holds the count.  A View owns nothing, and an owner gives it out.  The
// constructor from raw parts is private, and only the base layer reaches
// it, through detail::view_over_, so no pointer that a caller holds becomes
// a View.  The seal refuses std::bit_cast, so no bytes become a View
// either.
//
// The operations:
//
//   size()            the count of elements
//   for (x : view)    each element, with the count as the only bound
//   window(o, c)      the c elements from o, or no value when they do not
//                     fit: the one explicit branch of the family
//   window<N>(o)      the same, as a View of the fixed extent N
//   copy(dst, src)    the elements of src into dst, which has the same
//                     count by its type or by contract
//   fill(dst, value)  value into each element of dst
//
// A View<T> converts to a View<T const>, and a View<T, N> converts to a
// View<T>.  No conversion adds a const or removes it the other way, and no
// conversion gives a fixed extent: window<N> is the checked road.
//
// The window is the fit test of a caller.  A producer that appends a run
// to a segment asks the segment for the window of the run, and the empty
// result is its slow path.  The check of the window is then the only check
// of the append, and the copy into the window compiles to the memmove of
// the raw form.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

// The extent of a View whose count is a value and not a part of its type.
inline constexpr std::size_t dynamic_extent = ~std::size_t{0};

// The element of a View: an object type that is not an array and not
// volatile.  A const element gives a read-only View.
template <class T>
concept ViewElement = std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>;

// The largest count of a View whose element has element_bytes bytes.  A
// count larger than this has no address range, and a count of
// dynamic_extent marks an empty Option.  The bound is a function that is
// not a template, so no specialization changes it.
[[nodiscard]] constexpr std::size_t max_view_count(std::size_t element_bytes) noexcept {
    return static_cast<std::size_t>(__PTRDIFF_MAX__) / element_bytes;
}

// A fixed extent that a View can carry: not zero, and not past the largest
// count.
template <class T, std::size_t Extent>
concept ViewExtent = Extent == dynamic_extent || (Extent > 0 && Extent <= max_view_count(sizeof(T)));

// An element that copy() and fill() can write.  The write is a byte copy,
// so it must agree with the assignment of the element, and the element
// must start its lifetime with its bytes.
template <class T>
concept RegionCopyElement = std::is_trivially_copyable_v<T> && std::is_trivially_copy_assignable_v<T>
                         && std::is_implicit_lifetime_v<T> && !std::is_const_v<T>;

template <class T, std::size_t Extent = dynamic_extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class View;

namespace detail {

template <std::size_t Extent>
struct ViewCount {
    [[nodiscard]] static constexpr std::size_t value() noexcept { return Extent; }
};

template <>
struct ViewCount<dynamic_extent> {
    std::size_t count = 0;
    [[nodiscard]] constexpr std::size_t value() const noexcept { return count; }
};

// The door from raw parts to a View, for the owners of the base layer.
// The detail-namespace guard keeps each other caller out.
template <class T>
[[nodiscard]] constexpr View<T> view_over_(T* first, std::size_t count) noexcept;

template <std::size_t Extent, class T>
[[nodiscard]] constexpr View<T, Extent> view_over_(T* first) noexcept;

template <class T, std::size_t Extent>
[[nodiscard]] constexpr T* first_of_(View<T, Extent> view) noexcept;

}  // namespace detail

// The position of a loop over a View.  It comes only from begin() and
// end() of a View, and the seal refuses std::bit_cast and a lifetime start
// over bytes.  A cursor knows the end of its View.  A read or a step at the
// end ends the process in each build, so a cursor never leaves its View.
// A loop compares the cursor with the end before each read and each step,
// and the optimizer removes the two checks there, so the count of the View
// is the only bound of the loop.
template <class Element>
class ViewCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, std::size_t Extent>
        requires ViewElement<T> && ViewExtent<T, Extent>
    friend class View;

    constexpr ViewCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr Element& operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a View");
        }
        return *at_;
    }

    constexpr ViewCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ViewCursor const& left, ViewCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class View {
    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, std::size_t OtherExtent>
        requires ViewElement<U> && ViewExtent<U, OtherExtent>
    friend class View;

    template <class U>
    friend constexpr View<U> detail::view_over_(U* first, std::size_t count) noexcept;
    template <std::size_t OtherExtent, class U>
    friend constexpr View<U, OtherExtent> detail::view_over_(U* first) noexcept;
    template <class U, std::size_t OtherExtent>
    friend constexpr U* detail::first_of_(View<U, OtherExtent> view) noexcept;
    friend struct niche<View>;

    constexpr View(T* first, std::size_t count) noexcept
        requires(Extent == dynamic_extent)
        : first_{first}, count_{count} {}

    constexpr explicit View(T* first) noexcept
        requires(Extent != dynamic_extent)
        : first_{first} {}

public:
    using element_type = T;
    static constexpr std::size_t extent = Extent;

    // The empty View of a dynamic extent.  A View of a fixed extent always
    // borrows its elements, so it has no empty state.
    constexpr View() noexcept
        requires(Extent == dynamic_extent)
    = default;

    // Adds a const to the element, or drops the fixed extent, or both.
    template <class U, std::size_t OtherExtent>
        requires std::same_as<std::remove_const_t<U>, std::remove_const_t<T>>
              && (std::is_const_v<T> || !std::is_const_v<U>) && (Extent == dynamic_extent || Extent == OtherExtent)
              && (!std::same_as<U, T> || Extent != OtherExtent)
    constexpr View(View<U, OtherExtent> other) noexcept : first_{other.first_} {
        if constexpr (Extent == dynamic_extent) count_.count = other.size();
    }

    // The door checks each count against max_view_count in each build, and
    // the optimizer reads that bound in each build: a window of the View is
    // then never the empty value of an Option, with no check.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = count_.value();
        [[assume(count <= max_view_count(sizeof(T)))]];
        return count;
    }

    [[nodiscard]] constexpr ViewCursor<T> begin() const noexcept { return ViewCursor<T>{first_, first_ + size()}; }
    [[nodiscard]] constexpr ViewCursor<T> end() const noexcept {
        return ViewCursor<T>{first_ + size(), first_ + size()};
    }

    // The count elements from offset, or no value when they do not fit.
    // The result is an Option<View<T>>.  The return type is deduced, so the
    // Option is named only where View<T> is a complete type.
    [[nodiscard]] constexpr auto window(std::size_t offset, std::size_t count) const noexcept {
        using Result = Option<View<T>>;
        if (offset > size() || count > size() - offset) [[unlikely]] {
            return Result{none};
        }
        // The window fits, so its count is at most the count of this View,
        // which its door bounds.  The optimizer then knows that the count
        // is not the empty value of the niche, and the Option needs no test.
        [[assume(count <= max_view_count(sizeof(T)))]];
        return Result::some(View<T>{first_ + offset, count});
    }

    // The Window elements from offset, as an Option<View<T, Window>>, or
    // no value when they do not fit.
    template <std::size_t Window>
        requires ViewExtent<T, Window> && (Window != dynamic_extent)
    [[nodiscard]] constexpr auto window(std::size_t offset) const noexcept {
        using Result = Option<View<T, Window>>;
        if (offset > size() || Window > size() - offset) [[unlikely]] {
            return Result{none};
        }
        // The window fits and holds at least one element, so this View holds
        // one, and its door gave it a pointer that is not null.  The
        // optimizer then knows that the window is not the empty value of
        // the niche, and the Option needs no test.
        [[assume(first_ != nullptr)]];
        return Result::some(View<T, Window>{first_ + offset});
    }
};

// The empty value of an Option of a View.  A View of a fixed extent has a
// null pointer only as this value.  A View of a dynamic extent has the
// count dynamic_extent only as this value, because no View holds more
// than max_view_count elements.
template <class T, std::size_t Extent>
struct niche<View<T, Extent>> {
    [[nodiscard]] static constexpr View<T, Extent> empty() noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T, Extent>{nullptr, dynamic_extent};
        } else {
            return View<T, Extent>{static_cast<T*>(nullptr)};
        }
    }
    [[nodiscard]] static constexpr bool is_empty(View<T, Extent> const& view) noexcept {
        if constexpr (Extent == dynamic_extent) {
            return view.count_.count == dynamic_extent;
        } else {
            return view.first_ == nullptr;
        }
    }
};

namespace detail {

template <class T>
[[nodiscard]] constexpr View<T> view_over_(T* first, std::size_t count) noexcept {
    if (count > max_view_count(sizeof(T))) [[unlikely]] {
        fatal("view_over_ got a count past the largest count of a View");
    }
    if (first == nullptr && count != 0) [[unlikely]] {
        fatal("view_over_ got a null pointer with a count");
    }
    return View<T>{first, count};
}

template <std::size_t Extent, class T>
[[nodiscard]] constexpr View<T, Extent> view_over_(T* first) noexcept {
    if (first == nullptr) [[unlikely]] {
        fatal("view_over_ got a null pointer for a View of a fixed extent");
    }
    return View<T, Extent>{first};
}

template <class T, std::size_t Extent>
[[nodiscard]] constexpr T* first_of_(View<T, Extent> view) noexcept {
    return view.first_;
}

}  // namespace detail

// Copies the elements of src into dst.  The two have the same count: by
// the type for a fixed extent, and by contract for a dynamic one.  The
// copy is a memmove, so two views of one run can overlap.  The cost is
// the memmove of the raw form.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T>
void copy(View<T, Extent> dst, std::type_identity_t<View<T const, Extent>> src) noexcept {
    if (dst.size() != src.size()) [[unlikely]] {
        fatal("copy got two Views with different counts");
    }
    __builtin_memmove(detail::first_of_(dst), detail::first_of_(src), dst.size() * sizeof(T));
}

// Writes value into each element of dst.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T>
constexpr void fill(View<T, Extent> dst, std::type_identity_t<T> const& value) noexcept {
    for (T& element : dst) {
        element = value;
    }
}

}  // namespace foundation::core
