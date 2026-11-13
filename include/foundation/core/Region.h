#pragma once

// The Region family: one contiguous run of elements, and its views.
//
// View<T> borrows a run of elements: a pointer and a count.  View<T, N>
// borrows a run of exactly N elements: a pointer alone, because the type
// holds the count.  A View owns nothing, and an owner gives it out.  The
// constructors from raw parts take the key of detail::ViewDoor, and only the
// base layer reaches them, so no pointer that a caller holds becomes a View.
// An owner that proves its run, such as a FixedArray, a FixedList or a
// HeapArray, uses them with no check and no test of a null pointer.
// detail::view_over_ checks raw parts and then uses them.  The seal refuses
// std::bit_cast, so no bytes become a View either.
//
// The operations:
//
//   size()            the count of elements
//   for (x : view)    each element, with the count as the only bound
//   window(o, c)      the c elements from o, or no value when they do not
//                     fit: the one explicit branch of the family
//   window<N>(o)      the same, as a View of the fixed extent N
//   copy(dst, src)    the elements of src into dst.  Two Views of one
//                     fixed extent cannot fail.  Two Views of a dynamic
//                     extent give a Result, with LengthMismatch when their
//                     counts differ
//   fill(dst, value)  value into each element of dst.  An element of one
//                     byte takes one memset, the code of the raw form
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
//
// The loop of a View gives the cursor of foundation/core/Cursor.h, which a
// TextView of foundation/core/Text.h gives too.
//
// get(position) gives an Option of a Ref, so this header includes the Ref
// family and defines the body of get itself.  A unit that includes only
// this header then cannot give get a body of its own.
//
// THE PAIRS
//   view.enumerate()    each element of a View of a fixed extent with its
//                       proven index: a Record<Index<N>, Ref<T>>.  A View of
//                       a dynamic extent has no indices(), so it has no
//                       enumerate()
//   zip(first, second)  each pair of the elements at one position of two
//                       Views: a Record<Ref<A>, Ref<B>>.  Two Views of one
//                       fixed extent give the loop, because the type proves
//                       the counts equal.  Two Views of different fixed
//                       extents do not compile.  When a View has a dynamic
//                       extent, zip gives a Result, with LengthMismatch when
//                       the counts differ
//
// A Record refuses a reference part, so a pair borrows each element through
// a Ref, which is never null and has the size of a pointer.  The binding
// `auto [index, element] = pair;` gives the parts, and *element reaches the
// element.  This header includes the Record family and defines the ranges
// and the cursors of the pairs, so each friend of View has its definition
// here.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Cursor.h>
#include <foundation/core/Index.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
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

// The key of the doors of the base layer to the parts of a View: the
// constructors from raw parts, which check nothing, and first_of_, which
// gives the pointer of a View.  An owner that proves its run, such as a
// FixedArray, a FixedList or a HeapArray, makes its View through the key, so
// its View has no compare, also with -fno-delete-null-pointer-checks.
// view_over_ checks raw parts of a run and then uses the same key.  ViewDoor
// is the one friend of the key, and only ViewDoor builds it.  The key, the
// door and first_of_ are no templates, so no explicit specialization of a
// template reaches the parts of a View.  The rule owners keeps each caller
// outside the base out of detail.
struct ViewDoor;

class [[nodiscard]] ViewKey final {
    friend struct ViewDoor;

    constexpr ViewKey() noexcept {}
    constexpr ViewKey(ViewKey const&) noexcept {}

public:
    ViewKey& operator=(ViewKey const&) = delete("a key of a View lives for one construction");
};

struct ViewDoor {
    [[nodiscard]] static constexpr ViewKey key() noexcept { return ViewKey{}; }
};

// The body of each get of the Region family: the element at position of the
// count elements from first, as an Option of a Ref with the brand of the
// public call, or no value at or past the count.  first is not null when
// count is above zero.
template <::foundation::brand::IsBrand Brand, class T>
[[nodiscard]] constexpr auto element_at_(T* first, std::size_t count, std::size_t position) noexcept {
    if (position >= count) [[unlikely]] {
        return Option<Ref<T, Brand>>{none};
    }
    return Option<Ref<T, Brand>>::some(RefDoor::lend<Brand>(first[position]));
}

// The loops of the iteration verbs reverse(), windows<W>(), chunks<W>(),
// enumerate() and zip(first, second) of a View, and their cursors.  View
// names the ranges as friends, and this header defines each of them.
template <class Element>
class ReverseRange;
template <class Element>
class ReverseCursor;
template <class Element, std::size_t Width>
class WindowRange;
template <class Element, std::size_t Width>
class WindowCursor;
template <class Element, std::size_t Width>
class ChunkRange;
template <class Element, std::size_t Width>
class ChunkCursor;
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand>
class EnumerateRange;
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor;
template <class First, class Second, ::foundation::brand::IsBrand Brand>
class ZipRange;
template <class First, class Second, class Brand>
class ZipCursor;

}  // namespace detail

template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class View {
    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, std::size_t OtherExtent>
        requires ViewElement<U> && ViewExtent<U, OtherExtent>
    friend class View;

    friend struct niche<View>;
    // The ranges of the iteration verbs read the pointer member of their View
    // directly.
    template <class Element>
    friend class detail::ReverseRange;
    template <class Element, std::size_t Width>
    friend class detail::WindowRange;
    template <class Element, std::size_t Width>
    friend class detail::WindowCursor;
    template <class Element, std::size_t Width>
    friend class detail::ChunkCursor;
    template <class Element, std::size_t Width>
    friend class detail::ChunkRange;
    template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand>
    friend class detail::EnumerateRange;
    template <class First, class Second, ::foundation::brand::IsBrand Brand>
    friend class detail::ZipRange;

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

    // The door of the base layer: the count elements from first, with no
    // check.  The caller proves the elements live, the count at most
    // max_view_count, and first not null when the count is above zero.  An
    // owner whose invariant gives these facts, such as a FixedList or a
    // HeapArray, makes its View through it.
    constexpr View(detail::ViewKey const& /*key*/, T* first, std::size_t count) noexcept
        requires(Extent == dynamic_extent)
        : View{first, count} {}

    // The door of the base layer for a fixed extent: the Extent elements
    // from first, with no check.  The caller proves the elements live and
    // first not null.  An owner of Extent elements, such as a FixedArray,
    // makes its View through it.
    constexpr View(detail::ViewKey const& /*key*/, T* first) noexcept
        requires(Extent != dynamic_extent)
        : View{first} {}

    // The pointer of the first element, for the owners and the free
    // operations of the base layer, such as copy() and fill().  It is a
    // friend that is no template, so no explicit specialization reaches it,
    // and the key keeps each caller outside the base out.  Argument-dependent
    // lookup finds it: first_of_(detail::ViewDoor::key(), view).
    [[nodiscard]] friend constexpr T* first_of_(detail::ViewKey const& /*key*/, View view) noexcept {
        return view.first_;
    }

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

    [[nodiscard]] constexpr ViewCursor<T> begin() const noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T> end() const noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }

    // The element at a proven index of a View of a fixed extent.  The type
    // of the index proves it below the extent, so the read has no check.
    // An index below a smaller count is below the extent too.
    template <std::size_t Count>
        requires(Extent != dynamic_extent && Count <= Extent)
    [[nodiscard]] constexpr T& at(Index<Count> index) const noexcept {
        return first_[index.raw()];
    }

    // The element at the constant index I, which the type proves below the
    // extent.
    template <std::size_t I>
        requires(Extent != dynamic_extent && I < Extent)
    [[nodiscard]] constexpr T& at(StaticIndex<I>) const noexcept {
        return first_[I];
    }

    // Each index of a View of a fixed extent, in order: the loop
    // `for (Index<Extent> index : view.indices())`.
    [[nodiscard]] static constexpr auto indices() noexcept
        requires(Extent != dynamic_extent)
    {
        return IndexRange<Extent>{};
    }

    // The element at position, as an Option of a Ref with the fresh brand of
    // the call, or no value at or past the count: one compare.  The pack
    // refuses each explicit argument, so no caller names the brand.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get(std::size_t position) const noexcept {
        return detail::element_at_<Fresh>(first_, size(), position);
    }

    // The loop `for (auto [index, element] : view.enumerate())` reads each
    // element with its proven index, in order.  Each step gives a Record of
    // the Index<Extent> of indices() and a Ref to the element, and each Ref
    // of one call has the fresh brand of that call.  Only a View of a fixed
    // extent has indices(), so only that View has enumerate().  The pack
    // refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0 && Extent != dynamic_extent)
    [[nodiscard]] constexpr auto enumerate() const noexcept {
        return detail::EnumerateRange<T, Extent, Fresh>{*this};
    }

    // The loop `for (T& element : view.reverse())` reads each element one
    // time, from the last element.
    [[nodiscard]] constexpr auto reverse() const noexcept { return detail::ReverseRange<T>{View<T>{*this}}; }

    // The loop `for (View<T, Width> run : view.windows<Width>())` reads each
    // run of Width consecutive elements, with a step of one element.  A View
    // of fewer than Width elements gives no run.  A View of a fixed extent
    // refuses a Width past its extent at compile time.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto windows() const noexcept {
        return detail::WindowRange<T, Width>{View<T>{*this}};
    }

    // The loop `for (View<T, Width> run : view.chunks<Width>())` reads each
    // run of Width elements, with a step of Width elements.  rest() of the
    // range gives the View of the last size() % Width elements, which no
    // run holds.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto chunks() const noexcept {
        return detail::ChunkRange<T, Width>{View<T>{*this}};
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
        // the niche, and the Option needs no test.  The assume reads a local
        // pointer.  With an assume on the member first_, GCC 16 kept the
        // View in memory and kept the test of the niche, and the probe
        // view_window_fixed of a Release build had 15 more instructions.
        T* const first = first_ + offset;
        [[assume(first != nullptr)]];
        return Result::some(View<T, Window>{first});
    }
};

// The empty value of an Option of a View.  A View of a fixed extent has a
// null pointer only as this value.  A View of a dynamic extent has the
// count dynamic_extent only as this value, because no View holds more
// than max_view_count elements.
template <class T, std::size_t Extent>
struct niche<View<T, Extent>> {
    [[nodiscard]] static constexpr View<T, Extent> empty(niche_key<View<T, Extent>> const&) noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T, Extent>{nullptr, dynamic_extent};
        } else {
            return View<T, Extent>{static_cast<T*>(nullptr)};
        }
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<View<T, Extent>> const&,
                                                 View<T, Extent> const& view) noexcept {
        if constexpr (Extent == dynamic_extent) {
            return view.count_.count == dynamic_extent;
        } else {
            return view.first_ == nullptr;
        }
    }
};

namespace detail {

// The checked door from raw parts to a View, for the owners of the base
// layer that cannot prove their parts.  It ends the process for a count past
// max_view_count and for a null pointer with a count, and then it makes the
// View through the key.  View names it as no friend, so an explicit
// specialization of it reaches no private member of a View.  The rule owners
// keeps each caller outside the base out.
template <class T>
[[nodiscard]] constexpr View<T> view_over_(T* first, std::size_t count) noexcept {
    if (count > max_view_count(sizeof(T))) [[unlikely]] {
        fatal("view_over_ got a count past the largest count of a View");
    }
    if (first == nullptr && count != 0) [[unlikely]] {
        fatal("view_over_ got a null pointer with a count");
    }
    return View<T>{ViewDoor::key(), first, count};
}

template <std::size_t Extent, class T>
[[nodiscard]] constexpr View<T, Extent> view_over_(T* first) noexcept {
    if (first == nullptr) [[unlikely]] {
        fatal("view_over_ got a null pointer for a View of a fixed extent");
    }
    return View<T, Extent>{ViewDoor::key(), first};
}

// The cursors of the iteration verbs obey the rule of a ViewCursor: each
// cursor knows the end of its loop, a read or a step at the end ends the
// process in each build, and the optimizer removes the two checks in a loop
// that compares the cursor with the end first.  A cursor comes only from
// its range, and the seal refuses std::bit_cast and a lifetime start over
// bytes.  A range comes only from a View, so it holds a run that a View
// proves.

// The position of a reverse loop: the place after the next element to read,
// and the first element of the View, which is the end of the loop.
template <class Element>
class ReverseCursor {
    Element* after_ = nullptr;
    Element* first_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ReverseRange<Element>;

    constexpr ReverseCursor(Element* after, Element* first) noexcept : after_{after}, first_{first} {}

public:
    [[nodiscard]] constexpr Element& operator*() const noexcept {
        if (after_ == first_) [[unlikely]] {
            fatal("a read through the end cursor of a reverse loop over a View");
        }
        return *(after_ - 1);
    }

    constexpr ReverseCursor& operator++() noexcept {
        if (after_ == first_) [[unlikely]] {
            fatal("a step past the end cursor of a reverse loop over a View");
        }
        --after_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ReverseCursor const& left, ReverseCursor const& right) noexcept {
        return left.after_ == right.after_;
    }
};

// The elements of a View, from the last element.
template <class Element>
class ReverseRange {
    View<Element> elements_;

public:
    constexpr explicit ReverseRange(View<Element> elements) noexcept : elements_{elements} {}

    [[nodiscard]] constexpr ReverseCursor<Element> begin() const noexcept {
        Element* const first = elements_.first_;
        return ReverseCursor<Element>{first + elements_.size(), first};
    }
    [[nodiscard]] constexpr ReverseCursor<Element> end() const noexcept {
        Element* const first = elements_.first_;
        return ReverseCursor<Element>{first, first};
    }
};

// The position of a loop over the windows of a View: the first element of
// the next window, and the place after the first element of the last
// window, which is the end of the loop.
template <class Element, std::size_t Width>
class WindowCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class WindowRange<Element, Width>;

    constexpr WindowCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the windows of a View");
        }
        return View<Element, Width>{at_};
    }

    constexpr WindowCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the windows of a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(WindowCursor const& left, WindowCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// Each run of Width consecutive elements of a View, with a step of one.  A
// View of size n holds n - Width + 1 runs, and a View of fewer than Width
// elements holds no run.
template <class Element, std::size_t Width>
class WindowRange {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* end_() const noexcept {
        Element* const first = elements_.first_;
        std::size_t const size = elements_.size();
        return size < Width ? first : first + (size - Width + 1);
    }

public:
    constexpr explicit WindowRange(View<Element> elements) noexcept : elements_{elements} {}

    [[nodiscard]] constexpr WindowCursor<Element, Width> begin() const noexcept {
        return WindowCursor<Element, Width>{elements_.first_, end_()};
    }
    [[nodiscard]] constexpr WindowCursor<Element, Width> end() const noexcept {
        Element* const end = end_();
        return WindowCursor<Element, Width>{end, end};
    }
};

// The position of a loop over the chunks of a View: the first element of
// the next chunk, and the first element after the last whole chunk, which
// is the end of the loop.
template <class Element, std::size_t Width>
class ChunkCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ChunkRange<Element, Width>;

    constexpr ChunkCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the chunks of a View");
        }
        return View<Element, Width>{at_};
    }

    constexpr ChunkCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the chunks of a View");
        }
        at_ += Width;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ChunkCursor const& left, ChunkCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// Each run of Width elements of a View, with a step of Width, and the rest:
// the last size() % Width elements, which no run holds.
template <class Element, std::size_t Width>
class ChunkRange {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* end_() const noexcept {
        return elements_.first_ + (elements_.size() / Width) * Width;
    }

public:
    constexpr explicit ChunkRange(View<Element> elements) noexcept : elements_{elements} {}

    [[nodiscard]] constexpr ChunkCursor<Element, Width> begin() const noexcept {
        return ChunkCursor<Element, Width>{elements_.first_, end_()};
    }
    [[nodiscard]] constexpr ChunkCursor<Element, Width> end() const noexcept {
        Element* const end = end_();
        return ChunkCursor<Element, Width>{end, end};
    }

    // The View of the elements after the last whole run.
    [[nodiscard]] constexpr View<Element> rest() const noexcept {
        return View<Element>{end_(), elements_.size() % Width};
    }
};

// The cursors of enumerate() and zip obey the rule of a ViewCursor: each
// cursor knows the end of its loop, a read or a step at the end ends the
// process in each build, and the optimizer removes the two checks in a loop
// that compares the cursor with the end first.  A cursor comes only from its
// range, and the seal refuses std::bit_cast and a lifetime start over bytes.

// The position of a loop over the elements of a View<Element, Count> with
// their indexes: the first element, and the position of the next element.
// The read compares the position with Count first, so the Index of each
// step is an index of indices(), and the type of the View proves the read
// below its count.  The position is a full word, also when the Index holds
// a 32-bit word, so the loop needs one loop variable: the position and the
// offset of the element at the same time.  The read makes the Index through
// the door of Index.h, because the compare before it proves the position
// below Count.  Index<Count>::check there gave the loop of the probe
// view_enumerate_sum one more instruction on each target.  Each Ref has the
// brand of the call of enumerate() that made the range.
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor {
    Element* first_ = nullptr;
    std::size_t at_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class EnumerateRange<Element, Count, Brand>;

    constexpr EnumerateCursor(Element* first, std::size_t at) noexcept : first_{first}, at_{at} {}

public:
    [[nodiscard]] constexpr Record<Index<Count>, Ref<Element, Brand>> operator*() const noexcept {
        if (at_ >= Count) [[unlikely]] {
            fatal("a read through the end cursor of an enumerate loop over a View");
        }
        return {Index<Count>{IndexDoor::key(), at_}, RefDoor::lend<Brand>(first_[at_])};
    }

    constexpr EnumerateCursor& operator++() noexcept {
        if (at_ >= Count) [[unlikely]] {
            fatal("a step past the end cursor of an enumerate loop over a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(EnumerateCursor const& left, EnumerateCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The elements of a View of the fixed extent Count, each with its index.
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand>
class EnumerateRange {
    Element* first_ = nullptr;

public:
    constexpr explicit EnumerateRange(View<Element, Count> elements) noexcept : first_{elements.first_} {}

    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> begin() const noexcept {
        return EnumerateCursor<Element, Count, Brand>{first_, 0};
    }
    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> end() const noexcept {
        return EnumerateCursor<Element, Count, Brand>{first_, Count};
    }
};

// The position of a loop over the pairs of two Views of one count: the
// next element and the end of the first View, and the next element of the
// second View.  The range proved the two counts equal, so the end of the
// first View is the end of the loop, and the second pointer never passes
// the end of its View.  Each Ref has the brand of the call of zip that made
// the range.
template <class First, class Second, class Brand>
class ZipCursor {
    First* first_at_ = nullptr;
    First* first_end_ = nullptr;
    Second* second_at_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ZipRange<First, Second, Brand>;

    constexpr ZipCursor(First* first_at, First* first_end, Second* second_at) noexcept
        : first_at_{first_at}, first_end_{first_end}, second_at_{second_at} {}

public:
    [[nodiscard]] constexpr Record<Ref<First, Brand>, Ref<Second, Brand>> operator*() const noexcept {
        if (first_at_ == first_end_) [[unlikely]] {
            fatal("a read through the end cursor of a zip loop over two Views");
        }
        return {RefDoor::lend<Brand>(*first_at_), RefDoor::lend<Brand>(*second_at_)};
    }

    constexpr ZipCursor& operator++() noexcept {
        if (first_at_ == first_end_) [[unlikely]] {
            fatal("a step past the end cursor of a zip loop over two Views");
        }
        ++first_at_;
        ++second_at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ZipCursor const& left, ZipCursor const& right) noexcept {
        return left.first_at_ == right.first_at_;
    }
};

// The pairs of the elements of two Views of one count.  The constructor
// compares the two counts, and two counts that differ end the process in
// each build.  zip compares them first, or the types of the Views prove
// them equal, so the optimizer removes this compare.
template <class First, class Second, ::foundation::brand::IsBrand Brand>
class ZipRange {
    View<First> first_;
    Second* second_ = nullptr;

public:
    constexpr ZipRange(View<First> first, View<Second> second) noexcept : first_{first}, second_{second.first_} {
        if (first.size() != second.size()) [[unlikely]] {
            fatal("a zip of two Views of {} and {} elements", first.size(), second.size());
        }
    }

    // The count of pairs: the count of each View.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return first_.size(); }

    [[nodiscard]] constexpr ZipCursor<First, Second, Brand> begin() const noexcept {
        First* const first = first_.first_;
        return ZipCursor<First, Second, Brand>{first, first + first_.size(), second_};
    }
    [[nodiscard]] constexpr ZipCursor<First, Second, Brand> end() const noexcept {
        First* const end = first_.first_ + first_.size();
        return ZipCursor<First, Second, Brand>{end, end, second_ + first_.size()};
    }
};

}  // namespace detail

// The error of a copy between two Views of a dynamic extent whose counts
// differ.
struct LengthMismatch final {};

// The pairs of two Views of one fixed extent.  The type proves the counts
// equal, so the loop needs no compare before its first step.  Each Ref of
// one call has the fresh brand of that call, and the pack refuses each
// explicit argument.  The result type holds the brand in
// undeducible_brand_t, so neither a target type nor the declared type of a
// specialization sets it (foundation/Brand.h).
template <class... Refused, class First, class Second, std::size_t Extent,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0 && Extent != dynamic_extent)
[[nodiscard]] constexpr detail::ZipRange<First, Second, ::foundation::brand::undeducible_brand_t<Fresh>>
zip(View<First, Extent> first, View<Second, Extent> second) noexcept {
    return detail::ZipRange<First, Second, Fresh>{first, second};
}

// Two Views of different fixed extents have no pair for each element of the
// longer View, and the types show it at compile time.
template <class First, std::size_t FirstExtent, class Second, std::size_t SecondExtent>
    requires(FirstExtent != dynamic_extent && SecondExtent != dynamic_extent && FirstExtent != SecondExtent)
void zip(View<First, FirstExtent>, View<Second, SecondExtent>) =
    delete("two Views of different fixed extents have different counts, so the longer View has elements with no pair");

// The pairs of two Views when a View has a dynamic extent: the loop, or
// LengthMismatch when the counts differ.  One compare.  Each Ref of one call
// has the fresh brand of that call, and deduction cannot set it.
template <class... Refused, class First, std::size_t FirstExtent, class Second, std::size_t SecondExtent,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0 && (FirstExtent == dynamic_extent || SecondExtent == dynamic_extent))
[[nodiscard]] constexpr Result<detail::ZipRange<First, Second, ::foundation::brand::undeducible_brand_t<Fresh>>,
                               LengthMismatch>
zip(View<First, FirstExtent> first, View<Second, SecondExtent> second) noexcept {
    if (first.size() != second.size()) {
        return err(LengthMismatch{});
    }
    return detail::ZipRange<First, Second, Fresh>{first, second};
}

// Copies the elements of src into dst, two Views of one fixed extent.  The
// type gives the two the same count, so the copy cannot fail.  The copy is
// a memmove, so two views of one run can overlap.  A View<T> converts to a
// View<T const> of the same run, so no type proves that the two do not
// overlap.  On x86_64, glibc resolves memcpy and memmove to one function,
// so the memmove runs the code of the memcpy of the raw form.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T> && (Extent != dynamic_extent)
void copy(View<T, Extent> dst, std::type_identity_t<View<T const, Extent>> src) noexcept {
    __builtin_memmove(first_of_(detail::ViewDoor::key(), dst), first_of_(detail::ViewDoor::key(), src),
                      Extent * sizeof(T));
}

// Copies the elements of src into dst, two Views of a dynamic extent.  The
// counts can differ, so the copy gives a Result: LengthMismatch when they
// differ, and the copy then writes nothing.  A window of the count of src
// has that count, so the optimizer removes the test of a copy into it.
template <class T>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(View<T> dst, std::type_identity_t<View<T const>> src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    __builtin_memmove(first_of_(detail::ViewDoor::key(), dst), first_of_(detail::ViewDoor::key(), src),
                      dst.size() * sizeof(T));
    return Unit{};
}

namespace detail {

// An element that fill() writes with one memset: an element of one byte
// that decode reads as one byte.  decode refuses a padding bit and a
// bit-field.  An empty class has one byte of padding and no value, so the
// guard also refuses it by name.
template <class T>
concept ByteFillElement = sizeof(T) == 1 && !std::is_empty_v<T> && DecodableFrom<std::uint8_t, T>;

}  // namespace detail

// Writes value into each element of dst.  For an element of one byte, the
// fill is one memset of the byte of value.  GCC gives the memset builtin
// the rule nonnull-if-nonzero, so an empty View with a null pointer needs
// no test.  A constant evaluation and each other element keep the loop.
// The element is trivially copyable, and its byte is a value of the
// element, so the write of the byte is an assignment.  -Wclass-memaccess
// refuses a memset through a pointer to a class with a member initializer,
// so the call gives the builtin a void*.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T>
constexpr void fill(View<T, Extent> dst, std::type_identity_t<T> const& value) noexcept {
    if constexpr (detail::ByteFillElement<T>) {
        if !consteval {
            std::uint8_t const byte = decode<std::uint8_t>(value).expect("decode gives a byte for each value");
            __builtin_memset(static_cast<void*>(first_of_(detail::ViewDoor::key(), dst)), byte, dst.size());
            return;
        }
    }
    for (T& element : dst) {
        element = value;
    }
}

}  // namespace foundation::core
