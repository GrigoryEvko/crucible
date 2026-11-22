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
//   copy(dst, src) and write_each(out, op, inputs...) of a ViewMut
//                     the doors of a kernel, after copy(View, View): a
//                     copy or an elementwise loop into the run of a frame,
//                     with restrict pointers when the brands prove that no
//                     input overlaps the output
//   fill(dst, value) of a ViewMut
//                     the fill of a View into the run of a frame
//   byte_view(run)    the bytes of a run of unsigned chars as std::byte, of
//                     the same count and extent and the same const
//   offset_of(whole, part)
//                     the position of part inside whole, or no value when
//                     part does not lie inside whole or starts between two
//                     of its elements: the inverse of window(o, c)
//   offset_of(whole, e)
//                     the position of the element e inside whole, or no
//                     value when e is no element of whole: the inverse of
//                     get(position)
//   classify_overlap(left, right)
//                     how the runs of two Views share storage: Disjoint,
//                     InPlace or Shared, for a door that reads one run and
//                     writes the other on two threads
//
// A View<T> converts to a View<T const>, and a View<T, N> converts to a
// View<T>.  No conversion adds a const or removes it the other way, and no
// conversion gives a fixed extent: window<N> is the checked road.
//
// THE UNIQUE VIEW
//   ViewMut<T, Brand>   the unique borrow of the run of an owner that a frame
//                       door holds (foundation/core/Frame.h).  The frame doors
//                       of the owners give it, and no other code makes one.  It
//                       does not copy, and its brand is the brand of its frame
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
// THE WALK
//   view.walk()         the head of a lazy chain of adapters and terminals of
//                       foundation/core/Walk.h, such as
//                       view.walk().map(fn).fold(init, step).  Each step of
//                       the root stage detail::ViewStage gives T&, in the
//                       counted loop of the View
//   reverse(), windows<W>(), chunks<W>(), enumerate()
//                       the iteration verbs: each gives a walk whose root
//                       stage is in this header, so `for (x : view.reverse())`
//                       and view.reverse().map(fn).fold(init, step) both work
// Walk.h is below this header, and it gives dynamic_extent and
// LengthMismatch.
//
// THE FOREIGN DOORS
//   mint_foreign_view(first, count)   the read-only View of a run that a
//                       foreign ABI gives, such as the bytes of the fuzz
//                       entry or the data of a tensor.  Each bit pattern of
//                       the element is a value
//   mint_foreign_text(first, count)   the TextView of such a run of
//                       characters
//   foreign_c_str(text)   the address of the first character of a CText,
//                       for a foreign reader that reads up to the zero
//   foreign_first(view)   the address of the first element of a View, for a
//                       raw loop: a foreign reader, the measured loop of a
//                       bench, or a probe of raw behavior
// The two mints check the count, the null pointer and the alignment, and
// they cannot prove that the run lives.  A call of each door stands only at
// the edge of a foreign ABI, in an opt-out region of the class ABI, or in the
// measured loop of a bench, in an opt-out region of the class MEASURE.  A
// call of foreign_first can also stand in a probe of raw behavior, in an
// opt-out region of the class PROBE, such as a test that reads the address of
// the first element of a run as an integer.
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
//   first.walk().zip(second.walk()), first.walk().try_zip(second.walk())
//                       each pair of the elements at one position of two
//                       Views: a Record<Ref<A>, Ref<B>> (foundation/core/Walk.h).
//                       Two walks of one fixed extent give the loop, because
//                       the type proves the counts equal.  When a walk has a
//                       dynamic extent, try_zip gives a Result, with
//                       LengthMismatch when the counts differ
//
// A Record refuses a reference part, so a pair borrows each element through
// a Ref, which is never null and has the size of a pointer.  The binding
// `auto [index, element] = pair;` gives the parts, and *element reaches the
// element.  This header includes the Record family and defines the stage and
// the cursor of enumerate().
//
// THE TRANSFER
//   A View refers to its elements as a reference does (refers of
//   foundation/Transfer.h).  So a View<T const> moves to another thread and
//   two threads read it when T is Shareable.  A View<T> that is not const
//   moves to another thread only when two threads can write a T, because a
//   const View<T> gives each element as T&, and a copy of it writes too.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Cursor.h>
#include <foundation/core/Index.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/core/Walk.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

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

// The key of the constructors of a View from raw parts, which check nothing.
// An owner that proves its run, such as a FixedArray, a FixedList or a
// HeapArray, makes its View through the key, so its View has no compare, also
// with -fno-delete-null-pointer-checks.  view_over_ checks raw parts of a run
// and then uses the same key.  ViewDoor is the one friend of the key, and only
// ViewDoor builds it.  The key and the door are no templates, so no explicit
// specialization of a template reaches the constructors.  The rule owners
// keeps each caller outside the base out of detail.
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

// The tag of first_of_, the read of the pointer of the first element of a
// View or a ViewMut by the base layer.  The raw loops, the kernels and the
// system doors of the base read the pointer of a View that they hold.  The
// read makes no View, so each unit of the base builds the tag.  The tag is in
// namespace detail, and its constructor is explicit, so a caller outside the
// base names detail to call first_of_, and the rule owners keeps it out.
// first_of_ is a friend that is no template, so no explicit specialization
// reaches it.  first_of_ takes a const reference to a temporary tag: GCC 16
// then gives the code of the former key, and a tag by value or a reference
// to a named tag gave a sort loop four more instructions.
struct BaseLayerRead final {
    explicit BaseLayerRead() = default;
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

// The root stages of the walks of the iteration verbs reverse(),
// windows<W>(), chunks<W>() and enumerate() of a View, and their cursors.
// This header defines each of them.
template <class Element>
class ReverseStage;
template <class Element>
class ReverseCursor;
template <class Element, std::size_t Width>
class WindowStage;
template <class Element, std::size_t Width>
class WindowCursor;
template <class Element, std::size_t Width>
class ChunkStage;
template <class Element, std::size_t Width>
class ChunkCursor;
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
class EnumerateStage;
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor;

// The root stage of the walk of a View.
template <class T, std::size_t Extent>
class ViewStage;

}  // namespace detail

template <class T, std::size_t Extent>
    requires ViewElement<T> && ViewExtent<T, Extent>
class[[= ::foundation::transfer::refers{}]] View {
    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, std::size_t OtherExtent>
        requires ViewElement<U> && ViewExtent<U, OtherExtent>
    friend class View;

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
    // and the tag keeps each caller outside the base out.  Argument-dependent
    // lookup finds it: first_of_(detail::BaseLayerRead{}, view).
    [[nodiscard]] friend constexpr T* first_of_(detail::BaseLayerRead const& /*tag*/, View view) noexcept {
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

    // The walk of the elements of this View, in order: the head of a chain of
    // foundation/core/Walk.h, such as view.walk().map(fn).fold(init, step).
    // Each step gives T&, and a View of a fixed extent gives a walk of that
    // extent.
    [[nodiscard]] constexpr Walk<detail::ViewStage<T, Extent>> walk() const noexcept {
        return Walk<detail::ViewStage<T, Extent>>{detail::ViewStage<T, Extent>{*this}};
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

    // The walk of each element with its proven index, in order:
    // `for (auto [index, element] : view.enumerate())`.  Each step gives a
    // Record of the Index<Extent> of indices() and a Ref to the element, and
    // each Ref of one call has the fresh brand of that call.  Only a View of a
    // fixed extent has indices(), so only that View has enumerate().  A limit
    // or a skip of the walk keeps the index of each element in the View.  The
    // pack refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0 && Extent != dynamic_extent)
    [[nodiscard]] constexpr auto enumerate() const noexcept {
        using Stage = detail::EnumerateStage<T, Extent, Fresh, Extent>;
        return Walk<Stage>{Stage{*this}};
    }

    // The walk of each element one time, from the last element:
    // `for (T& element : view.reverse())`.
    [[nodiscard]] constexpr auto reverse() const noexcept {
        return Walk<detail::ReverseStage<T>>{detail::ReverseStage<T>{View<T>{*this}}};
    }

    // The walk of each run of Width consecutive elements, with a step of one
    // element: `for (View<T, Width> run : view.windows<Width>())`.  A View of
    // fewer than Width elements gives no run.  A View of a fixed extent
    // refuses a Width past its extent at compile time.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto windows() const noexcept {
        return Walk<detail::WindowStage<T, Width>>{detail::WindowStage<T, Width>{View<T>{*this}}};
    }

    // The walk of each run of Width elements, with a step of Width elements:
    // `for (View<T, Width> run : view.chunks<Width>())`.  rest() of the walk
    // gives the View of the elements after its last run: the last
    // size() % Width elements.
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Extent == dynamic_extent || Width <= Extent)
    [[nodiscard]] constexpr auto chunks() const noexcept {
        return Walk<detail::ChunkStage<T, Width>>{detail::ChunkStage<T, Width>{View<T>{*this}}};
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

// The root stage of the walk of a View (THE STAGES of foundation/core/Walk.h):
// the counted loop over the elements, by reference.  A View of a fixed
// extent keeps the count in the type of the stage, and a limit or a skip
// gives a stage of a dynamic extent.
template <class T, std::size_t Extent>
class ViewStage final {
    View<T, Extent> elements_;

public:
    using element_reference = T&;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = Extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ViewStage(View<T, Extent> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        T* const first = first_of_(BaseLayerRead{}, elements_);
        std::size_t const count = elements_.size();
        for (std::size_t position = 0; position < count; ++position) {
            sink(first[position]);
        }
    }

    // The loop of a walk that can stop is the pointer loop of the cursor of a
    // View: one compare of the pointer with the end, and no count to keep.
    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        T* const first = first_of_(BaseLayerRead{}, elements_);
        T* const end = first + elements_.size();
        for (T* at = first; at != end; ++at) {
            if (sink(*at) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return elements_.size(); }

    // The element at position, which the caller proves below size_() (THE
    // STAGES of foundation/core/Walk.h).
    [[nodiscard]] constexpr T& at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return first_of_(BaseLayerRead{}, elements_)[position];
    }

    // The first count elements, or each element when count is larger.
    [[nodiscard]] constexpr ViewStage<T, dynamic_extent> limited_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        return ViewStage<T, dynamic_extent>{
            View<T>{ViewDoor::key(), first_of_(BaseLayerRead{}, elements_), count < size ? count : size}};
    }

    // The elements after the first count, or none when count is larger.
    [[nodiscard]] constexpr ViewStage<T, dynamic_extent> skipped_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const dropped = count < size ? count : size;
        return ViewStage<T, dynamic_extent>{
            View<T>{ViewDoor::key(), first_of_(BaseLayerRead{}, elements_) + dropped, size - dropped}};
    }

    // The cursor of `for (x : walk)`: the cursor of the View.
    [[nodiscard]] constexpr ViewCursor<T> begin_() const noexcept { return elements_.begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end_() const noexcept { return elements_.end(); }
};

}  // namespace detail

// The bytes of a View as an argument of a report: two lower-case
// hexadecimal digits for each byte, in the order of the View (THE
// HEXADECIMAL TEXT of foundation/core/Report.h).  It borrows the bytes, as
// the View does, so it lives only for the call of the report.
class HexBytes final {
    View<std::byte const> bytes_{};

public:
    // The mark by which the concept Formattable of foundation/core/Report.h
    // knows the class.
    using report_mark = detail::HexBytesMark;

    constexpr explicit HexBytes(View<std::byte const> bytes) noexcept : bytes_{bytes} {}

    // The bytes whose digits the report writes.
    [[nodiscard]] constexpr View<std::byte const> view() const noexcept { return bytes_; }
};

// ── The unique view ────────────────────────────────────────────────────

// The unique borrow of the run of an owner that a frame door holds
// (foundation/core/Frame.h).  The brand is the brand of the frame.  A
// ViewMut does not copy, so a body cannot hold two ViewMuts of one frame.  A
// move leaves the source with its run, and the rule use-after-move refuses a
// later use of the source.  The move constructor and the destructor are
// trivial, so the ABI passes a ViewMut in two registers, and a ViewMut of a
// fixed extent in one.  The seal makes the move assignment user-provided, so
// std::bit_cast builds no ViewMut.
//
//   size(), for (x : view)   as a View gives them
//   get(position)            the element as an Option<Ref<T, Brand>>, with the
//                            brand of the frame, or no value at or past the
//                            count: one compare
//   get_mut(position)        the element as an Option<RefMut<T, Brand>>, the
//                            unique borrow of the element, which does not
//                            copy, or no value at or past the count: one
//                            compare
//   view()                   the plain View of the run, with no brand
//   reborrow(std::move(view), body)
//                            body gets a ViewMut of the run, and the door
//                            gives the ViewMut back after the body
//   split_at(std::move(view), offset)
//                            the parts [0, offset) and [offset, size()), each a
//                            ViewMut of a dynamic extent, or no value when the
//                            offset is past the count
//   split_at<Offset>(std::move(view))
//                            the same two parts of a ViewMut of a fixed
//                            extent, each of a fixed extent, with no check
//   split_chunks(std::move(view), width)
//                            the range of the parts of width elements, and a
//                            last part of the rest
//
// The splits are free functions.  A member function whose return type names
// ViewMut would be part of the members that the walk of a linear payload
// reads (foundation/Linear.h), and its type depends on that walk.
//
// A ViewMut<T, Brand, N> holds exactly N elements, as a View<T, N> does, and
// it converts to the ViewMut<T, Brand> of the same run.  The frame door of a
// FixedArray gives the fixed extent.
//
// THE SPLIT
//   A split takes the parent by move, so the parts are the only writers of
//   the run, and no two parts share an element.  Each part keeps the brand of
//   its parent.  So two parts of one split prove no difference to a door of
//   a kernel: a copy between them takes the memmove, and write_each takes the
//   loop that the compiler guards with an alias check.  Each path is correct
//   for two runs that do not overlap.  A fresh brand for each part would let
//   the doors take the restrict path, and it is a later step.  The split of
//   a named ViewMut and of a const one do not compile.
//
// A const reference to a ViewMut is a shared borrow that keeps the brand of
// the frame.  It reads the run, and it does not write it.  A door of a kernel
// reads its inputs through such a reference, so the brands tell it that two
// runs do not overlap (THE ALIAS PROOF of Frame.h).
//
// The class states refers_unique (foundation/Transfer.h).  A ViewMut moves to
// another thread when its element can move.  The read first_of_ gives the
// first element for writing from a const reference too, so two threads share
// a const reference of a ViewMut only when they can write its element.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent = dynamic_extent>
    requires ViewElement<T> && (!std::is_const_v<T>) && ViewExtent<T, Extent>
class [[nodiscard]][[= ::foundation::transfer::refers_unique{}]] ViewMut final {
    T* first_ = nullptr;
    [[no_unique_address]] detail::ViewCount<Extent> count_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class U, ::foundation::brand::IsBrand OtherBrand, std::size_t OtherExtent>
        requires ViewElement<U> && (!std::is_const_v<U>) && ViewExtent<U, OtherExtent>
    friend class ViewMut;

public:
    using element_type = T;
    using brand_type = Brand;
    static constexpr std::size_t extent = Extent;

    // The run of an owner that a frame door holds, with the key that only
    // FrameDoor builds.  The owner gave the View, so the View bounds the
    // count and holds a pointer that is not null for each count above zero.
    constexpr ViewMut(detail::FrameKey const& /*key*/, View<T, Extent> run) noexcept
        : first_{first_of_(detail::BaseLayerRead{}, run)} {
        if constexpr (Extent == dynamic_extent) count_.count = run.size();
    }

    // The ViewMut of the same run with a dynamic extent.  The source moves
    // in, so the run keeps one writer.
    template <std::size_t OtherExtent>
        requires(Extent == dynamic_extent && OtherExtent != dynamic_extent)
    constexpr ViewMut(ViewMut<T, Brand, OtherExtent>&& other) noexcept : first_{other.first_}, count_{OtherExtent} {}

    ViewMut(ViewMut const&) = delete("a ViewMut is the one writer of its run: a copy would be a second writer");
    ViewMut& operator=(ViewMut const&) = delete("a ViewMut is the one writer of its run: a copy would be a second "
                                                "writer");
    constexpr ViewMut(ViewMut&&) noexcept = default;
    constexpr ViewMut& operator=(ViewMut&&) noexcept = default;
    ~ViewMut() = default;

    // The pointer of the first element, for the free operations of the base
    // layer, such as copy() and write_each().  The tag keeps each caller
    // outside the base out, as for first_of_ of a View.
    [[nodiscard]] friend constexpr T* first_of_(detail::BaseLayerRead const& /*tag*/, ViewMut const& view) noexcept {
        return view.first_;
    }

    // The View of the run bounds the count, so the optimizer reads that
    // bound in each build, as size() of a View does.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = count_.value();
        [[assume(count <= max_view_count(sizeof(T)))]];
        return count;
    }

    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept {
        return ViewCursor<T>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept {
        return ViewCursor<T const>{detail::ViewCursorDoor::key(), first_, first_ + size()};
    }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept {
        return ViewCursor<T const>{detail::ViewCursorDoor::key(), first_ + size(), first_ + size()};
    }

    // The element at position, as an Option<Ref<T, Brand>> with the brand of
    // the frame.  The brand keeps the Ref out of the result of the frame
    // (FrameResult of Choice.h).  Each frame has a brand of its own, so each
    // frame makes a ViewMut class of its own.  The member is a template, so
    // that class names no Option type and checks no constraint of one until a
    // body reads an element.  The pack refuses each explicit argument.
    template <class... Refused>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get(std::size_t position) & noexcept {
        return detail::element_at_<Brand>(first_, size(), position);
    }
    template <class... Refused>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get(std::size_t position) const& noexcept {
        return detail::element_at_<Brand>(static_cast<T const*>(first_), size(), position);
    }

    // The element at position, as an Option<RefMut<T, Brand>> with the brand
    // of the frame.  A RefMut does not copy, so the element has one writer
    // for each call.  The brand keeps the RefMut out of the result of the
    // frame.  The member is a template with a deduced return type for the
    // reason of get(): the class of each frame names no Option type, and it
    // checks no constraint of one until a body writes an element.  A const
    // ViewMut writes nothing, so it gives no RefMut.
    template <class... Refused>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto get_mut(std::size_t position) & noexcept {
        if (position >= size()) [[unlikely]] {
            return Option<RefMut<T, Brand>>{none};
        }
        return Option<RefMut<T, Brand>>::some(RefMut<T, Brand>{detail::FrameDoor::key(), first_[position]});
    }
    template <class... Refused>
    void get_mut(std::size_t) const& = delete("a const ViewMut writes nothing, so it gives no writer of an element");

    // The plain View of the run, for an operation that takes a View, such as
    // fill().  The View has no brand, so it proves no difference of two runs.
    // It has the extent of the ViewMut.
    [[nodiscard]] constexpr View<T, Extent> view() & noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T>{detail::ViewDoor::key(), first_, size()};
        } else {
            return View<T, Extent>{detail::ViewDoor::key(), first_};
        }
    }
    [[nodiscard]] constexpr View<T const, Extent> view() const& noexcept {
        if constexpr (Extent == dynamic_extent) {
            return View<T const>{detail::ViewDoor::key(), first_, size()};
        } else {
            return View<T const, Extent>{detail::ViewDoor::key(), first_};
        }
    }
};

// The range of the parts of a split into chunks (THE SPLIT): next() gives the
// next part, from the first element, and no value after the last part.  Each
// call gives each part one time, so the range gives no second writer of a
// part.  The range does not copy, and it holds the borrow of the run that is
// left.
template <class T, ::foundation::brand::IsBrand Brand>
class [[nodiscard]] ViewMutChunks final {
    T* next_ = nullptr;
    std::size_t rest_ = 0;
    std::size_t width_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The chunks of width elements of run, with the key that only FrameDoor
    // builds.  The width is above zero.
    constexpr ViewMutChunks(detail::FrameKey const& /*key*/, View<T> run, std::size_t width) noexcept
        : next_{first_of_(detail::BaseLayerRead{}, run)}, rest_{run.size()}, width_{width} {}

    ViewMutChunks(ViewMutChunks const&) = delete("a copy of the range would give each part a second time");
    ViewMutChunks& operator=(ViewMutChunks const&) = delete("a copy of the range would give each part a second time");
    constexpr ViewMutChunks(ViewMutChunks&&) noexcept = default;
    constexpr ViewMutChunks& operator=(ViewMutChunks&&) noexcept = default;
    ~ViewMutChunks() = default;

    // The count of the parts that next() still gives.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return rest_ / width_ + (rest_ % width_ != 0 ? 1 : 0); }

    // The next part, of width elements, or of the rest when fewer are left,
    // or no value after the last part.  Complexity: O(1).
    [[nodiscard]] constexpr Option<ViewMut<T, Brand>> next() & noexcept {
        if (rest_ == 0) return Option<ViewMut<T, Brand>>{none};
        std::size_t const count = rest_ < width_ ? rest_ : width_;
        T* const first = next_;
        next_ += count;
        rest_ -= count;
        return Option<ViewMut<T, Brand>>::some(
            ViewMut<T, Brand>{detail::FrameDoor::key(), View<T>{detail::ViewDoor::key(), first, count}});
    }
};

// The parts [0, offset) and [offset, size()) of the run of whole, by THE
// SPLIT, or no value when the offset is past the count.  Each part has a
// dynamic extent and the brand of whole.  Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr Option<Record<ViewMut<T, Brand>, ViewMut<T, Brand>>> split_at(ViewMut<T, Brand, Extent>&& whole,
                                                                                      std::size_t offset) noexcept {
    using Parts = Record<ViewMut<T, Brand>, ViewMut<T, Brand>>;
    std::size_t const count = whole.size();
    if (offset > count) [[unlikely]] {
        return Option<Parts>{none};
    }
    T* const first = first_of_(detail::BaseLayerRead{}, whole);
    return Option<Parts>::some(Parts{
        ViewMut<T, Brand>{detail::FrameDoor::key(), View<T>{detail::ViewDoor::key(), first, offset}},
        ViewMut<T, Brand>{detail::FrameDoor::key(), View<T>{detail::ViewDoor::key(), first + offset, count - offset}}});
}

// The parts [0, Offset) and [Offset, Extent) of the run of a fixed extent of
// whole, by THE SPLIT.  The type proves the offset inside the run, so the
// split has no check, and each part has a fixed extent and the brand of
// whole.  Complexity: O(1).
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires(Extent != dynamic_extent && Offset > 0 && Offset < Extent)
[[nodiscard]] constexpr Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>
split_at(ViewMut<T, Brand, Extent>&& whole) noexcept {
    T* const first = first_of_(detail::BaseLayerRead{}, whole);
    return Record<ViewMut<T, Brand, Offset>, ViewMut<T, Brand, Extent - Offset>>{
        ViewMut<T, Brand, Offset>{detail::FrameDoor::key(), View<T, Offset>{detail::ViewDoor::key(), first}},
        ViewMut<T, Brand, Extent - Offset>{detail::FrameDoor::key(),
                                           View<T, Extent - Offset>{detail::ViewDoor::key(), first + Offset}}};
}

// The range of the parts of width elements of the run of whole, from the
// first element, and a last part of the size() % width elements when that
// count is not zero, by THE SPLIT.  Each part has a dynamic extent and the
// brand of whole.  A width of zero ends the process.  Complexity: O(1), and
// O(1) for each part.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
[[nodiscard]] constexpr ViewMutChunks<T, Brand> split_chunks(ViewMut<T, Brand, Extent>&& whole,
                                                             std::size_t width) noexcept {
    if (width == 0) [[unlikely]] {
        fatal("a split of a ViewMut into chunks of zero elements");
    }
    return ViewMutChunks<T, Brand>{detail::FrameDoor::key(), View<T>{whole.view()}, width};
}

// A named ViewMut stays with the caller, so the parts of its split would be
// second writers of its run.  A const ViewMut writes nothing, so it gives no
// writer of a part.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent>&,
              std::size_t) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent> const&,
              std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent>&) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <std::size_t Offset, class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_at(ViewMut<T, Brand, Extent> const&) =
    delete("a const ViewMut writes nothing, so it gives no writer of a part");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_chunks(ViewMut<T, Brand, Extent>&,
                  std::size_t) = delete("a split takes the ViewMut by move: pass std::move(view)");
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
void split_chunks(ViewMut<T, Brand, Extent> const&,
                  std::size_t) = delete("a const ViewMut writes nothing, so it gives no writer of a part");

namespace detail {

// The body of each frame door of an owner of the Region family.  body gets
// the ViewMut of run with the brand of the frame and the extent of run, and
// the door gives held back: alone for a body that gives void, and after the
// result in a Record for each other body.  held lives in the frame of the
// door, and run is the run of held.  The body runs before the Record moves
// held, because a braced list runs its parts in order.  The door gives up
// held here, so the helper takes it as an rvalue.
template <class Brand, class Owner, class T, std::size_t Extent, class Body>
[[nodiscard]] constexpr frame_return_t<body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Owner>
give_run_back_(Owner&& held, View<T, Extent> run, Body&& body) noexcept {
    using Given = body_result_t<Body&&, ViewMut<T, Brand, Extent>>;
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(ViewMut<T, Brand, Extent>{FrameDoor::key(), run});
        return static_cast<Owner&&>(held);
    } else {
        return frame_return_t<Given, Owner>{static_cast<Body&&>(body)(ViewMut<T, Brand, Extent>{FrameDoor::key(), run}),
                                            static_cast<Owner&&>(held)};
    }
}

}  // namespace detail

// The body of a frame door of an owner whose elements are T: it takes the
// ViewMut of the brand of the frame with no throw, and its result leaves the
// frame (FrameResult of Choice.h).  Extent is the extent of the run of the
// owner.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept FrameBody = std::is_nothrow_invocable_v<Body&&, ViewMut<T, Brand, Extent>>
                 && FrameResult<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Brand>;

// A body whose result does not leave the frame: the refused twin of each
// door binds to it.
template <class Body, class T, class Brand, std::size_t Extent = dynamic_extent>
concept RefusedFrameBody = std::is_nothrow_invocable_v<Body&&, ViewMut<T, Brand, Extent>>
                        && (!FrameResult<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>, Brand>);

// ── The reborrow ───────────────────────────────────────────────────────
//
// reborrow(std::move(view), body) gives body a ViewMut of the run of view,
// and gives view back after the body: alone for a body that gives void, and
// after the result in a Record for each other body.  The parent moves into
// the frame of the door, so the caller holds no name for it while the body
// runs, as a split takes its parent by move.  So the body holds the one
// writer of the run, and it can give it to a door that takes a ViewMut by
// value, such as fill, copy or write_each.  The result of the body leaves
// the frame under the rule of each frame door (FrameResult of Choice.h).
//
// The ViewMut of the body has the brand of the parent, and not a fresh
// brand.  A const reference to the parent can stay with the caller, and the
// move leaves the parent with its run.  A fresh brand would make the doors
// read the two as two runs, and a copy between them would take the memcpy
// on one run.  One brand takes the memmove, which is correct for each pair.

namespace detail {

// The run of view as a plain View of its extent.  Only the doors of this
// header use it, and each of them holds the ViewMut by move.
template <class T, class Brand, std::size_t Extent>
[[nodiscard]] constexpr View<T, Extent> run_of_(ViewMut<T, Brand, Extent> const& view) noexcept {
    T* const first = first_of_(BaseLayerRead{}, view);
    if constexpr (Extent == dynamic_extent) {
        return View<T>{ViewDoor::key(), first, view.size()};
    } else {
        return View<T, Extent>{ViewDoor::key(), first};
    }
}

}  // namespace detail

template <class T, class Brand, std::size_t Extent, class Body>
    requires FrameBody<Body, T, Brand, Extent>
[[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Brand, Extent>>,
                                               ViewMut<T, Brand, Extent>>
reborrow(ViewMut<T, Brand, Extent>&& view, Body&& body) noexcept {
    View<T, Extent> const run = detail::run_of_(view);
    return detail::give_run_back_<Brand>(std::move(view), run, static_cast<Body&&>(body));
}

template <class T, class Brand, std::size_t Extent, class Body>
    requires RefusedFrameBody<Body, T, Brand, Extent>
void reborrow(ViewMut<T, Brand, Extent>&&, Body&&) = delete(
    "the result of a reborrow leaves the frame: give a value with no reference, no address and no brand of the frame");

// A named ViewMut stays with the caller, so the ViewMut of the body would be
// a second writer of its run.  A const ViewMut writes nothing.
template <class T, class Brand, std::size_t Extent, class Body>
void reborrow(ViewMut<T, Brand, Extent>&,
              Body&&) = delete("a reborrow takes the ViewMut by move: pass std::move(view)");
template <class T, class Brand, std::size_t Extent, class Body>
void reborrow(ViewMut<T, Brand, Extent> const&,
              Body&&) = delete("a const ViewMut writes nothing, so it gives no writer to a body");

// ── The position of a run inside a run ─────────────────────────────────

// The two Views of offset_of: Views of one element type, with or without a
// const on each.
template <class Whole, class Part>
concept SameElement = std::same_as<std::remove_const_t<Whole>, std::remove_const_t<Part>>;

// The position of the first element of part inside whole, or no value when
// the elements of part do not all lie inside whole.  The position counts
// elements, not bytes, so whole.window(position, part.size()) is part.  A
// part that starts between two elements of whole gives no value.  An empty
// part at the end of whole gives the count of whole.  An empty View with no
// address, such as a default View, lies inside each other View with no
// address, at the position zero.
//
// At run time the function compares the two address ranges as integers, so
// two Views of different objects give no value and no undefined compare of
// pointers: O(1).  A constant evaluation reads no address as a word.  It
// compares the first address of part with the address of each position of
// whole: O(whole.size()).  There, an empty part of a different object can
// meet a compare with the address one past the end of an object.  Such a
// compare has no specified result, so such a call does not compile.
template <class Whole, std::size_t WholeExtent, class Part, std::size_t PartExtent>
    requires SameElement<Whole, Part>
[[nodiscard]] constexpr Option<std::size_t> offset_of(View<Whole, WholeExtent> whole,
                                                      View<Part, PartExtent> part) noexcept {
    Whole* const whole_first = first_of_(detail::BaseLayerRead{}, whole);
    Part* const part_first = first_of_(detail::BaseLayerRead{}, part);
    if consteval {
        // A part with an element starts at an element of whole, and only an
        // empty part can start at the end of whole.  The count of whole is
        // at most max_view_count, so the sum does not overflow.
        std::size_t const starts = part.size() == 0 ? whole.size() + 1 : whole.size();
        for (std::size_t position = 0; position < starts; ++position) {
            if (whole_first + position != part_first) continue;
            if (part.size() > whole.size() - position) return Option<std::size_t>{none};
            return Option<std::size_t>::some(position);
        }
        return Option<std::size_t>{none};
    } else {
        std::uintptr_t const whole_start = __builtin_bit_cast(std::uintptr_t, whole_first);
        std::uintptr_t const part_start = __builtin_bit_cast(std::uintptr_t, part_first);
        // The door of a View bounds its bytes by __PTRDIFF_MAX__, and an
        // object ends below the top of the address space, so no sum
        // overflows.
        std::uintptr_t const whole_end = whole_start + whole.size() * sizeof(Whole);
        std::uintptr_t const part_end = part_start + part.size() * sizeof(Part);
        if (part_start < whole_start || part_end > whole_end) return Option<std::size_t>{none};
        std::size_t const bytes = part_start - whole_start;
        if (bytes % sizeof(Whole) != 0) return Option<std::size_t>{none};
        return Option<std::size_t>::some(bytes / sizeof(Whole));
    }
}

// The position of element inside whole, or no value when element is no
// element of whole: the inverse of get(position).  The function gives the
// position of the run of one element at element, so it has the one body
// above, with its test of an address between two elements.  Complexity: O(1)
// at run time, and O(whole.size()) in a constant evaluation.
template <class Whole, std::size_t WholeExtent>
[[nodiscard]] constexpr Option<std::size_t> offset_of(View<Whole, WholeExtent> whole,
                                                      std::type_identity_t<Whole> const& element) noexcept {
    return offset_of(whole, View<Whole const, 1>{detail::ViewDoor::key(), __builtin_addressof(element)});
}

// A temporary, also the result of a conversion, is no element of a View.
template <class Whole, std::size_t WholeExtent>
void offset_of(View<Whole, WholeExtent>, std::type_identity_t<Whole> const&&) =
    delete("a temporary is no element of a View, so it has no position in one");

// How an input shares storage with an output: the answer of classify_overlap
// for two runs, and of the plan of the pointwise walk of
// foundation/core/Pointwise.h for two strided views.
enum class RunOverlap : std::uint8_t {
    // No element of the input is an element of the output, or a run is
    // empty.
    Disjoint = 0,
    // The input names the elements of the output at the same indexes.
    InPlace = 1,
    // The input can name an element of the output at a different index.
    Shared = 2,
};

// How the run of left shares storage with the run of right.  InPlace needs
// the same first element and the same count.  An empty run shares no byte,
// so it is Disjoint from each run.  A View has no brand, so two Views
// prove no difference of their runs (THE ALIAS PROOF of
// foundation/core/Frame.h), and a door that reads one run while a thread
// writes the other asks this function one time, before the threads start.
//
// At run time the function compares the two address ranges as integers, as
// offset_of does, so two Views of different objects give Disjoint and no
// undefined compare of pointers: O(1).  A constant evaluation reads no
// address as a word.  It compares the first address of each run with the
// address of each element of the other run: O(left.size() + right.size()).
template <class Left, std::size_t LeftExtent, class Right, std::size_t RightExtent>
    requires SameElement<Left, Right>
[[nodiscard]] constexpr RunOverlap classify_overlap(View<Left, LeftExtent> left,
                                                    View<Right, RightExtent> right) noexcept {
    if (left.size() == 0 || right.size() == 0) return RunOverlap::Disjoint;
    Left* const left_first = first_of_(detail::BaseLayerRead{}, left);
    Right* const right_first = first_of_(detail::BaseLayerRead{}, right);
    if consteval {
        // Each compared address is an element of its run, so no compare
        // meets the address one past the end of an object.
        if (left_first == right_first) return left.size() == right.size() ? RunOverlap::InPlace : RunOverlap::Shared;
        for (std::size_t position = 1; position < right.size(); ++position) {
            if (right_first + position == left_first) return RunOverlap::Shared;
        }
        for (std::size_t position = 1; position < left.size(); ++position) {
            if (left_first + position == right_first) return RunOverlap::Shared;
        }
        return RunOverlap::Disjoint;
    } else {
        std::uintptr_t const left_start = __builtin_bit_cast(std::uintptr_t, left_first);
        std::uintptr_t const right_start = __builtin_bit_cast(std::uintptr_t, right_first);
        // The door of a View bounds its bytes by __PTRDIFF_MAX__, and an
        // object ends below the top of the address space, so no sum
        // overflows.
        std::uintptr_t const left_end = left_start + left.size() * sizeof(Left);
        std::uintptr_t const right_end = right_start + right.size() * sizeof(Right);
        if (left_end <= right_start || right_end <= left_start) return RunOverlap::Disjoint;
        if (left_start == right_start && left.size() == right.size()) return RunOverlap::InPlace;
        return RunOverlap::Shared;
    }
}

// ── The doors of a foreign run ─────────────────────────────────────────

// The element of a run that a foreign ABI writes, such as the bytes of the
// fuzz entry, the data of a PyTorch tensor or a record of a BPF ring buffer.
// The foreign side can write each bit pattern, so each bit pattern of the
// element is a value: no bool, no enumeration with an enumerator and no
// pointer (BitValid).
template <class T>
concept ForeignElement = ViewElement<T> && !std::is_const_v<T> && BitValid<T>;

// The View of the count elements at first, which a foreign ABI gives.  The
// View only reads.  The mint ends the process for a count past
// max_view_count, for a null pointer with a count, and for a pointer that is
// not aligned for T.  It does not prove that the run lives: the caller keeps
// the foreign run alive while it uses the View.  So a call stands at the
// edge of a foreign ABI only, in an opt-out region of the class ABI.  The
// address of a run is no constant of the compile, so the mint runs only at
// run time.
template <class T>
    requires ForeignElement<T>
[[nodiscard]] View<T const> mint_foreign_view(T const* first, std::size_t count) noexcept {
    if (count != 0 && (__builtin_bit_cast(std::uintptr_t, first) & (alignof(T) - 1)) != 0) [[unlikely]] {
        fatal("mint_foreign_view got a pointer that is not aligned for its element of {} bytes", alignof(T));
    }
    return detail::view_over_(first, count);
}

// The TextView of the count characters at first, which a foreign ABI gives.
// The mint ends the process for a count past __PTRDIFF_MAX__ and for a null
// pointer with a count.  As for mint_foreign_view, the caller keeps the run
// alive, and a call stands only in an opt-out region of the class ABI.
[[nodiscard]] inline TextView mint_foreign_text(char const* first, std::size_t count) noexcept {
    if (count > max_view_count(1)) [[unlikely]] {
        fatal("mint_foreign_text got a count past the largest count of a TextView");
    }
    if (first == nullptr && count != 0) [[unlikely]] {
        fatal("mint_foreign_text got a null pointer with a count");
    }
    return detail::text_over_(first, count);
}

// The address of the first character of text, for a reader of a foreign ABI
// that reads up to the zero, such as a C caller that takes a char const*.
// The CText proves the zero after its last character and no zero before it,
// so the reader reads each character and the zero, and no other byte.  The
// address borrows the owner of the text, and the caller keeps the owner
// alive while the foreign side reads.  A call stands only at the edge of a
// foreign ABI, in an opt-out region of the class ABI.
[[nodiscard]] inline char const* foreign_c_str(CText text) noexcept { return detail::c_text_first_(text); }

// The address of the first element of a view, for a raw loop at the edge of
// the base: a reader of a foreign ABI, the measured loop of a bench, which
// indexes the run as the hardware sees it, or a probe of raw behavior.  The
// address borrows the owner of the run, and it carries no bound.  So the
// caller keeps the owner alive while it reads, reads only the count of the
// view, and stands in an opt-out region of the class ABI, MEASURE or PROBE.
// An empty view gives its address, which no reader reads.
template <class T, std::size_t Extent>
[[nodiscard]] constexpr T* foreign_first(View<T, Extent> view) noexcept {
    return first_of_(detail::BaseLayerRead{}, view);
}

namespace detail {

// The root stages of the iteration verbs give the walks of THE STAGES of
// foundation/core/Walk.h.  Each stage comes only from a View, so it holds a
// run that a View proves.  The cursors of the stages obey the rule of a
// ViewCursor: each cursor knows the end of its loop, a read or a step at the
// end ends the process in each build, and the optimizer removes the two
// checks in a loop that compares the cursor with the end first.  A cursor
// comes only from its stage, and the seal refuses std::bit_cast and a
// lifetime start over bytes.

// The position of a reverse loop: the place after the next element to read,
// and the first element of the View, which is the end of the loop.
template <class Element>
class ReverseCursor {
    Element* after_ = nullptr;
    Element* first_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class ReverseStage<Element>;

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

// The root stage of reverse(): the elements of a View, from the last
// element.  The loop is the backward loop of a pointer.
template <class Element>
class ReverseStage final {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

public:
    using element_reference = Element&;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ReverseStage(View<Element> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        for (Element* after = first + elements_.size(); after != first; --after) {
            sink(after[-1]);
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        for (Element* after = first + elements_.size(); after != first; --after) {
            if (sink(after[-1]) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return elements_.size(); }

    // The element at position from the last element, which the caller proves
    // below size_() (THE STAGES of foundation/core/Walk.h).
    [[nodiscard]] constexpr Element& at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return first_()[elements_.size() - 1 - position];
    }

    // The last count elements, or each element when count is larger.
    [[nodiscard]] constexpr ReverseStage limited_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const kept = count < size ? count : size;
        return ReverseStage{View<Element>{ViewDoor::key(), first_() + (size - kept), kept}};
    }

    // The elements before the last count, or none when count is larger.
    [[nodiscard]] constexpr ReverseStage skipped_(std::size_t count) && noexcept {
        std::size_t const size = elements_.size();
        std::size_t const dropped = count < size ? count : size;
        return ReverseStage{View<Element>{ViewDoor::key(), first_(), size - dropped}};
    }

    [[nodiscard]] constexpr ReverseCursor<Element> begin_() const noexcept {
        Element* const first = first_();
        return ReverseCursor<Element>{first + elements_.size(), first};
    }
    [[nodiscard]] constexpr ReverseCursor<Element> end_() const noexcept {
        Element* const first = first_();
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

    friend class WindowStage<Element, Width>;

    constexpr WindowCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the windows of a View");
        }
        return View<Element, Width>{ViewDoor::key(), at_};
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

// The root stage of windows<Width>(): each run of Width consecutive elements
// of a View, with a step of one.  A View of size n holds n - Width + 1 runs,
// and a View of fewer than Width elements holds no run.
template <class Element, std::size_t Width>
class WindowStage final {
    View<Element> elements_;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

    [[nodiscard]] constexpr std::size_t runs_() const noexcept {
        std::size_t const size = elements_.size();
        return size < Width ? 0 : size - Width + 1;
    }

    // The place after the first element of the last run.  The select of two
    // places is the code of the raw loop, where a select of two counts and an
    // add gave the probe view_windows_sum one more instruction.
    [[nodiscard]] constexpr Element* end_of_runs_() const noexcept {
        Element* const first = first_();
        std::size_t const size = elements_.size();
        return size < Width ? first : first + (size - Width + 1);
    }

public:
    using element_reference = View<Element, Width>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit WindowStage(View<Element> elements) noexcept : elements_{elements} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const end = end_of_runs_();
        for (Element* at = first_(); at != end; ++at) {
            sink(View<Element, Width>{ViewDoor::key(), at});
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const end = end_of_runs_();
        for (Element* at = first_(); at != end; ++at) {
            if (sink(View<Element, Width>{ViewDoor::key(), at}) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return runs_(); }

    // The run at position, which the caller proves below size_() (THE STAGES
    // of foundation/core/Walk.h).
    [[nodiscard]] constexpr View<Element, Width> at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return View<Element, Width>{ViewDoor::key(), first_() + position};
    }

    // The first count runs, or each run when count is larger.
    [[nodiscard]] constexpr WindowStage limited_(std::size_t count) && noexcept {
        std::size_t const runs = runs_();
        std::size_t const kept = count < runs ? count : runs;
        return WindowStage{View<Element>{ViewDoor::key(), first_(), kept == 0 ? 0 : kept + Width - 1}};
    }

    // The runs after the first count, or none when count is larger.
    [[nodiscard]] constexpr WindowStage skipped_(std::size_t count) && noexcept {
        std::size_t const runs = runs_();
        std::size_t const dropped = count < runs ? count : runs;
        return WindowStage{View<Element>{ViewDoor::key(), first_() + dropped, elements_.size() - dropped}};
    }

    [[nodiscard]] constexpr WindowCursor<Element, Width> begin_() const noexcept {
        return WindowCursor<Element, Width>{first_(), end_of_runs_()};
    }
    [[nodiscard]] constexpr WindowCursor<Element, Width> end_() const noexcept {
        Element* const end = end_of_runs_();
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

    friend class ChunkStage<Element, Width>;

    constexpr ChunkCursor(Element* at, Element* end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr View<Element, Width> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the chunks of a View");
        }
        return View<Element, Width>{ViewDoor::key(), at_};
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

// The root stage of chunks<Width>(): each run of Width elements of a View,
// with a step of Width, and the rest: the elements after the last run of the
// walk.  The stage holds the elements from its first run to the end of the
// View, and the count of its runs.  A chunk walk of a View has size() /
// Width runs, so its rest is the last size() % Width elements.  A limit
// keeps the run of the View and gives fewer runs, so its rest grows.  A skip
// drops the first runs, and the rest stays.
template <class Element, std::size_t Width>
class ChunkStage final {
    View<Element> elements_;
    std::size_t runs_ = 0;

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, elements_); }

    constexpr ChunkStage(View<Element> elements, std::size_t runs) noexcept : elements_{elements}, runs_{runs} {}

public:
    using element_reference = View<Element, Width>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit ChunkStage(View<Element> elements) noexcept
        : elements_{elements}, runs_{elements.size() / Width} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        Element* const end = first + runs_ * Width;
        for (Element* at = first; at != end; at += Width) {
            sink(View<Element, Width>{ViewDoor::key(), at});
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        Element* const first = first_();
        Element* const end = first + runs_ * Width;
        for (Element* at = first; at != end; at += Width) {
            if (sink(View<Element, Width>{ViewDoor::key(), at}) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return runs_; }

    // The run at position, which the caller proves below size_() (THE STAGES
    // of foundation/core/Walk.h).
    [[nodiscard]] constexpr View<Element, Width> at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return View<Element, Width>{ViewDoor::key(), first_() + position * Width};
    }

    // The first count runs, or each run when count is larger.
    [[nodiscard]] constexpr ChunkStage limited_(std::size_t count) && noexcept {
        return ChunkStage{elements_, count < runs_ ? count : runs_};
    }

    // The runs after the first count, or none when count is larger.
    [[nodiscard]] constexpr ChunkStage skipped_(std::size_t count) && noexcept {
        std::size_t const dropped = count < runs_ ? count : runs_;
        std::size_t const offset = dropped * Width;
        return ChunkStage{View<Element>{ViewDoor::key(), first_() + offset, elements_.size() - offset},
                          runs_ - dropped};
    }

    // The View of the elements after the last run of the walk.
    [[nodiscard]] constexpr View<Element> rest_() const noexcept {
        std::size_t const offset = runs_ * Width;
        return View<Element>{ViewDoor::key(), first_() + offset, elements_.size() - offset};
    }

    [[nodiscard]] constexpr ChunkCursor<Element, Width> begin_() const noexcept {
        Element* const first = first_();
        return ChunkCursor<Element, Width>{first, first + runs_ * Width};
    }
    [[nodiscard]] constexpr ChunkCursor<Element, Width> end_() const noexcept {
        Element* const end = first_() + runs_ * Width;
        return ChunkCursor<Element, Width>{end, end};
    }
};

// The position of a loop over the elements of a View<Element, Count> with
// their indexes: the first element of the View, the position of the next
// element, and the position of the end of the loop, which is at most Count.
// The read compares the position with the end first, so the Index of each
// step is an index of indices(), and the type of the View proves the read
// below its count.  The position is a full word, also when the Index holds
// a 32-bit word, so the loop needs one loop variable: the position and the
// offset of the element at the same time.  The read makes the Index through
// its check after the compare with the end.  The constructor holds the end
// at most Count, so the optimizer knows the answer of the check and removes
// it.  Each Ref has the brand of the call of enumerate() that made the walk.
template <class Element, std::size_t Count, class Brand>
class EnumerateCursor {
    Element* first_ = nullptr;
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class Other, std::size_t OtherCount, ::foundation::brand::IsBrand OtherBrand, std::size_t Extent>
    friend class EnumerateStage;

    // The cursor at the first element of run, which starts at the position
    // start of the View.  The loop ends after the last element of run.  The
    // stage holds the end of each run at most Count, and the bound in the
    // code shows that fact to the optimizer.
    constexpr EnumerateCursor(View<Element> run, std::size_t start) noexcept
        : first_{first_of_(BaseLayerRead{}, run) - start},
          at_{start},
          end_{start + run.size() < Count ? start + run.size() : Count} {}

public:
    [[nodiscard]] constexpr Record<Index<Count>, Ref<Element, Brand>> operator*() const noexcept {
        if (at_ >= end_) [[unlikely]] {
            fatal("a read through the end cursor of an enumerate loop over a View");
        }
        return {Index<Count>::check(at_).expect("the position of an enumerate cursor lies below its count"),
                RefDoor::lend<Brand>(first_[at_])};
    }

    constexpr EnumerateCursor& operator++() noexcept {
        if (at_ >= end_) [[unlikely]] {
            fatal("a step past the end cursor of an enumerate loop over a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(EnumerateCursor const& left, EnumerateCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The root stage of enumerate(): the elements of a View of the fixed extent
// Count, each with its index.  The stage holds the run that it walks and the
// position of its first element in the View, so a limit or a skip keeps the
// index of each element.  The root has the extent Count, and a limit or a
// skip gives a dynamic extent.  The run ends at or before the position Count,
// so each index of a pair is below Count.  The one public constructor takes
// a whole View of Count elements, and only a limit or a skip of a stage
// builds a run from a later position.
template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
class EnumerateStage final {
    View<Element, Extent> run_;
    std::size_t start_ = 0;

    template <class Other, std::size_t OtherCount, ::foundation::brand::IsBrand OtherBrand, std::size_t OtherExtent>
    friend class EnumerateStage;

    // The run from the position start of a View of Count elements.  The
    // caller proves start + run.size() at most Count.
    constexpr EnumerateStage(View<Element, Extent> run, std::size_t start) noexcept : run_{run}, start_{start} {}

    [[nodiscard]] constexpr Element* first_() const noexcept { return first_of_(BaseLayerRead{}, run_); }

    // The position of the first element of the run in the View.  The root
    // stage, of the extent Count, starts at the position 0.
    [[nodiscard]] constexpr std::size_t start_of_() const noexcept { return Extent == Count ? 0 : start_; }

    // The position after the last element of the run.  The invariant of the
    // stage holds it at most Count, and the bound in the code shows that fact
    // to the optimizer, so the check of each Index below it goes away.
    [[nodiscard]] constexpr std::size_t end_of_() const noexcept {
        std::size_t const end = start_of_() + run_.size();
        return end < Count ? end : Count;
    }

    // The element at the position at of the View with its index.  origin is
    // the first element of the View, and the caller compares at with
    // end_of_() first.
    [[nodiscard]] static constexpr Record<Index<Count>, Ref<Element, Brand>> pair_(Element* origin,
                                                                                   std::size_t at) noexcept {
        return {Index<Count>::check(at).expect("an enumerate position lies below the count of its View"),
                RefDoor::lend<Brand>(origin[at])};
    }

public:
    using element_reference = Record<Index<Count>, Ref<Element, Brand>>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = Extent;
    static constexpr bool cursor_borrows_stage = false;

    // Each element of a View of Count elements, from the position 0.
    constexpr explicit EnumerateStage(View<Element, Count> elements) noexcept
        requires(Extent == Count)
        : run_{elements} {}

    // The loops run over the positions of the run in the View, so the one
    // loop variable is the index and the offset of the element.
    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        std::size_t const start = start_of_();
        Element* const origin = first_() - start;
        std::size_t const end = end_of_();
        for (std::size_t at = start; at < end; ++at) {
            sink(pair_(origin, at));
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        std::size_t const start = start_of_();
        Element* const origin = first_() - start;
        std::size_t const end = end_of_();
        for (std::size_t at = start; at < end; ++at) {
            if (sink(pair_(origin, at)) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return run_.size(); }

    // The element at position with its index, which the caller proves below
    // size_() (THE STAGES of foundation/core/Walk.h).  A zip compares the
    // position with its own count, so the check of the Index stays for a run
    // that starts after the position 0.
    [[nodiscard]] constexpr element_reference at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        std::size_t const start = start_of_();
        return pair_(first_() - start, start + position);
    }

    // The first count elements, or each element when count is larger.
    [[nodiscard]] constexpr auto limited_(std::size_t count) && noexcept {
        std::size_t const size = run_.size();
        return EnumerateStage<Element, Count, Brand, dynamic_extent>{
            View<Element>{ViewDoor::key(), first_(), count < size ? count : size}, start_};
    }

    // The elements after the first count, or none when count is larger.
    [[nodiscard]] constexpr auto skipped_(std::size_t count) && noexcept {
        std::size_t const size = run_.size();
        std::size_t const dropped = count < size ? count : size;
        return EnumerateStage<Element, Count, Brand, dynamic_extent>{
            View<Element>{ViewDoor::key(), first_() + dropped, size - dropped}, start_ + dropped};
    }

    // The cursor reads the View from its first element, at the positions of
    // this run.  The end cursor is the cursor of the empty run after it.
    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> begin_() const noexcept {
        return EnumerateCursor<Element, Count, Brand>{View<Element>{run_}, start_};
    }
    [[nodiscard]] constexpr EnumerateCursor<Element, Count, Brand> end_() const noexcept {
        std::size_t const size = run_.size();
        return EnumerateCursor<Element, Count, Brand>{View<Element>{ViewDoor::key(), first_() + size, 0},
                                                      start_ + size};
    }
};

// The largest copy of a fixed extent, in bytes, that copy() does with the
// words of copy_small_.  GCC expands a memmove builtin of a constant size
// inline only where it optimizes for speed.  It calls the library memmove
// in a block that it thinks cold, and under AddressSanitizer for most
// sizes.  The words of copy_small_ give no call in each block of each build.
inline constexpr std::size_t small_copy_bytes = 64;

// The word of Width bytes, for one load of copy_small_: an unsigned integer,
// or a vector of bytes for 16 bytes or more.
template <std::size_t Width>
struct CopyWord;
template <>
struct CopyWord<1> {
    using type = std::uint8_t;
};
template <>
struct CopyWord<2> {
    using type = std::uint16_t;
};
template <>
struct CopyWord<4> {
    using type = std::uint32_t;
};
template <>
struct CopyWord<8> {
    using type = std::uint64_t;
};
template <>
struct CopyWord<16> {
    using type [[gnu::vector_size(16)]] = std::uint8_t;
};
template <>
struct CopyWord<32> {
    using type [[gnu::vector_size(32)]] = std::uint8_t;
};
template <>
struct CopyWord<64> {
    using type [[gnu::vector_size(64)]] = std::uint8_t;
};

// The width of the tail word of a rest of bytes: the smallest power of two
// that is at least bytes.
[[nodiscard]] consteval std::size_t copy_tail_width_(std::size_t bytes) noexcept {
    std::size_t width = 1;
    while (width < bytes) {
        width *= 2;
    }
    return width;
}

// The words of copy_small_ follow the vector width of the target, so they
// are in the target namespace (THE TARGET NAMESPACE of foundation/Platform.h).
inline namespace CRUCIBLE_SIMD_TARGET {

// The width of the widest word of copy_small_: the width of a vector
// register of the target.
#if defined(__AVX512F__)
inline constexpr std::size_t widest_copy_word = 64;
#elif defined(__AVX__)
inline constexpr std::size_t widest_copy_word = 32;
#else
inline constexpr std::size_t widest_copy_word = 16;
#endif

// The width of the first word of a copy of bytes: the largest power of two
// that is at most bytes and at most widest_copy_word.
[[nodiscard]] consteval std::size_t copy_word_width_(std::size_t bytes) noexcept {
    std::size_t width = widest_copy_word;
    while (width > bytes) {
        width /= 2;
    }
    return width;
}

// The word of Width bytes at from.  The size of the builtin is the size of
// a local word, so GCC folds the builtin to one load in each block.
template <std::size_t Width>
[[nodiscard]] inline typename CopyWord<Width>::type load_copy_word_(unsigned char const* from) noexcept {
    typename CopyWord<Width>::type word{};
    __builtin_memcpy(&word, from, Width);
    return word;
}

// Writes the Width bytes of word at to.  GCC folds the builtin to one store.
template <std::size_t Width>
inline void store_copy_word_(unsigned char* to, typename CopyWord<Width>::type word) noexcept {
    __builtin_memcpy(to, &word, Width);
}

// Copies Bytes bytes from source to target with words.  The copy loads the
// first word.  When one more word of its width fits after it, the copy
// copies the rest with this function.  Else a tail word ends at the end of
// the run, and it can overlap the first word.  The copy stores the first
// word last, so each load comes before each store and the two runs can
// overlap.
//
// An overlap of two words writes some bytes two times, with the same value
// each time.  The words are the words of the inline memmove of GCC for the
// same size.
template <std::size_t Bytes>
    requires(Bytes > 0 && Bytes <= small_copy_bytes)
inline void copy_small_(unsigned char* target, unsigned char const* source) noexcept {
    constexpr std::size_t width = copy_word_width_(Bytes);
    auto const first = load_copy_word_<width>(source);
    if constexpr (Bytes >= 2 * width) {
        copy_small_<Bytes - width>(target + width, source + width);
    } else if constexpr (Bytes > width) {
        constexpr std::size_t tail_width = copy_tail_width_(Bytes - width);
        auto const tail = load_copy_word_<tail_width>(source + (Bytes - tail_width));
        store_copy_word_<tail_width>(target + (Bytes - tail_width), tail);
    }
    store_copy_word_<width>(target, first);
}

}  // namespace CRUCIBLE_SIMD_TARGET
}  // namespace detail

// The copy of a fixed extent calls copy_small_, so it is in the target
// namespace too.
inline namespace CRUCIBLE_SIMD_TARGET {

// Copies the elements of src into dst, two Views of one fixed extent.  The
// type gives the two the same count, so the copy cannot fail.  Two Views of
// one run can overlap, because a View<T> converts to a View<T const> of the
// same run.  So the copy gives dst the values that src held before the
// copy.  A copy of at most detail::small_copy_bytes takes the words of
// detail::copy_small_, so a copy of a record calls no library function in
// each build.  A larger copy is one memmove builtin.  On x86_64, glibc
// resolves memcpy and memmove to one function.
template <class T, std::size_t Extent>
    requires RegionCopyElement<T> && (Extent != dynamic_extent)
void copy(View<T, Extent> dst, std::type_identity_t<View<T const, Extent>> src) noexcept {
    constexpr std::size_t bytes = Extent * sizeof(T);
    void* const target = first_of_(detail::BaseLayerRead{}, dst);
    void const* const source = first_of_(detail::BaseLayerRead{}, src);
    if constexpr (bytes <= detail::small_copy_bytes) {
        detail::copy_small_<bytes>(static_cast<unsigned char*>(target), static_cast<unsigned char const*>(source));
    } else {
        __builtin_memmove(target, source, bytes);
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

// Copies the elements of src into dst, two Views of a dynamic extent.  The
// counts can differ, so the copy gives a Result: LengthMismatch when they
// differ, and the copy then writes nothing.  A window of the count of src
// has that count, so the optimizer removes the test of a copy into it.
template <class T>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(View<T> dst, std::type_identity_t<View<T const>> src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    __builtin_memmove(first_of_(detail::BaseLayerRead{}, dst), first_of_(detail::BaseLayerRead{}, src),
                      dst.size() * sizeof(T));
    return Unit{};
}

// ── The doors of a kernel ──────────────────────────────────────────────
//
// A door of a kernel writes the run of a ViewMut and reads its inputs.  An
// input is a ViewMut through a const reference, which keeps the brand of its
// frame, or a View, which has no brand.  When the brand
// of an input differs from the brand of the output, the frames of the two
// hold two owners, and the two runs do not overlap (THE ALIAS PROOF of
// foundation/core/Frame.h).  When each input proves that, the door gives
// the compiler restrict pointers, so the loop needs no run-time alias check.
// Each other input takes the loop of plain pointers, which the compiler
// guards with an alias check, or the memmove of a copy.  The door takes the
// output by value, as the decision on the alias proof states.

// An input of a door of a kernel: a ViewMut or a View.  The door reads each
// input through a pointer to a const element.
template <class In>
concept KernelInput =
    ::foundation::reflect::IsInstanceOf<In, ^^ViewMut> || ::foundation::reflect::IsInstanceOf<In, ^^View>;

// An input whose run does not overlap the run of an output of the brand
// Brand: a ViewMut of a different brand (THE ALIAS PROOF of Frame.h).
template <class In, class Brand>
concept DisjointInput =
    ::foundation::reflect::IsInstanceOf<In, ^^ViewMut> && !std::same_as<typename In::brand_type, Brand>;

// The calls of write_each that take the restrict loop: each input is a
// ViewMut of a brand other than Brand, and no element of an input or of the
// output holds a borrow.  An element that holds a borrow, such as a View
// that the caller made before the frame of the output, could name the run of
// the output, and the op could read that run through it while the loop
// writes it.  So such an element takes the loop of plain pointers.
template <class T, class Brand, class... Inputs>
concept RestrictKernel = (DisjointInput<Inputs, Brand> && ...)
                      && HoldsNoBorrow<T> && (HoldsNoBorrow<std::remove_const_t<typename Inputs::element_type>> && ...);

// The op of write_each for an element T and the inputs: an object with no
// state, which takes one element of each input by const reference and gives
// a value that converts to T, with no throw.  An op with no state holds no
// capture, so it reaches no run of the call through a capture, and
// RestrictKernel keeps a borrow in an element out of the restrict loop.  The
// proof assumes that the op reaches no run through an object of static
// storage duration either.
template <class Op, class T, class... Inputs>
concept KernelOp = std::is_empty_v<Op> && std::is_nothrow_copy_constructible_v<Op> && (KernelInput<Inputs> && ...)
                && std::is_nothrow_invocable_r_v<T, Op const&, typename Inputs::element_type const&...>;

namespace detail {

// The loop of write_each when no input overlaps the output.  Each pointer is
// a restrict parameter, and GCC keeps the restrict of a parameter of an
// inlined function, so the vectorized loop has no alias check.  Complexity:
// O(count).
template <class T, class Op, class... Elements>
[[gnu::always_inline]] inline void write_each_disjoint_(T* __restrict out, std::size_t count, Op const& op,
                                                        Elements const* __restrict... inputs) noexcept {
    for (std::size_t position = 0; position != count; ++position) {
        out[position] = op(inputs[position]...);
    }
}

// The loop of write_each when an input can overlap the output, such as an
// input of the brand of the output or a View.  The loop reads each input at
// a position before it writes the output at that position.  Complexity:
// O(count).
template <class T, class Op, class... Elements>
[[gnu::always_inline]] inline void write_each_aliased_(T* out, std::size_t count, Op const& op,
                                                       Elements const*... inputs) noexcept {
    for (std::size_t position = 0; position != count; ++position) {
        out[position] = op(inputs[position]...);
    }
}

}  // namespace detail

// Copies the elements of src into dst, a ViewMut and the ViewMut of a frame.
// Two brands prove that the runs do not overlap, so the copy is one memcpy.
// One brand proves no difference, so the copy is the memmove of
// copy(View, View).  The counts can differ, so the copy gives a Result, with
// LengthMismatch when they differ, and the copy then writes nothing.
template <class T, class Brand, std::size_t Extent, class SourceBrand, std::size_t SourceExtent>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(ViewMut<T, Brand, Extent> dst,
                                                ViewMut<T, SourceBrand, SourceExtent> const& src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    void* const target = first_of_(detail::BaseLayerRead{}, dst);
    void const* const source = first_of_(detail::BaseLayerRead{}, src);
    if constexpr (std::is_same_v<Brand, SourceBrand>) {
        __builtin_memmove(target, source, dst.size() * sizeof(T));
    } else {
        __builtin_memcpy(target, source, dst.size() * sizeof(T));
    }
    return Unit{};
}

// Copies the elements of a View into a ViewMut.  A View has no brand, so the
// copy is one memmove.
template <class T, class Brand, std::size_t Extent>
    requires RegionCopyElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> copy(ViewMut<T, Brand, Extent> dst,
                                                std::type_identity_t<View<T const>> src) noexcept {
    if (dst.size() != src.size()) return err(LengthMismatch{});
    __builtin_memmove(first_of_(detail::BaseLayerRead{}, dst), first_of_(detail::BaseLayerRead{}, src),
                      dst.size() * sizeof(T));
    return Unit{};
}

// Writes op(input[i]...) into each element out[i], in the order of i.  Each
// input has the count of out, or the door gives LengthMismatch and writes
// nothing.  When each input is a ViewMut of a brand other than the brand of
// out, and no element holds a borrow (RestrictKernel), the loop reads through
// restrict pointers.  Each other call takes the loop of plain pointers.  The
// two loops give the same values, because the
// loop of plain pointers reads each input at a position before it writes out
// at that position.  Complexity: O(out.size()).
template <class T, class Brand, std::size_t Extent, class Op, class... Inputs>
    requires KernelOp<Op, T, Inputs...>
[[nodiscard]] Result<Unit, LengthMismatch> write_each(ViewMut<T, Brand, Extent> out, Op op,
                                                      Inputs const&... inputs) noexcept {
    std::size_t const count = out.size();
    if (((inputs.size() != count) || ...)) return err(LengthMismatch{});
    T* const target = first_of_(detail::BaseLayerRead{}, out);
    if constexpr (RestrictKernel<T, Brand, Inputs...>) {
        detail::write_each_disjoint_(target, count, op, first_of_(detail::BaseLayerRead{}, inputs)...);
    } else {
        detail::write_each_aliased_(target, count, op, first_of_(detail::BaseLayerRead{}, inputs)...);
    }
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
            __builtin_memset(static_cast<void*>(first_of_(detail::BaseLayerRead{}, dst)), byte, dst.size());
            return;
        }
    }
    for (T& element : dst) {
        element = value;
    }
}

// Writes value into each element of the run of dst, as fill(View, value)
// does.  The door takes the ViewMut by value, as copy and write_each do, so
// a body that writes more after the fill gives it through reborrow.
template <class T, class Brand, std::size_t Extent>
    requires RegionCopyElement<T>
constexpr void fill(ViewMut<T, Brand, Extent> dst, std::type_identity_t<T> const& value) noexcept {
    ::foundation::core::fill(detail::run_of_(dst), value);
}

// The bytes of a run of unsigned chars, as a View of std::byte of the same
// count and extent.  A source of unsigned chars, such as the input of a fuzz
// entry or the storage of a tensor, then reaches a reader or a door of
// std::byte.  A std::byte may read and write each object, so the View reads
// and writes the bytes of the run.  A run of unsigned char const gives bytes
// of std::byte const, so the const stays.  The cast of the address is no
// constant expression, so the operation is not constexpr.  Complexity: O(1).
template <class T, std::size_t Extent>
    requires std::same_as<std::remove_const_t<T>, unsigned char>
[[nodiscard]] View<std::conditional_t<std::is_const_v<T>, std::byte const, std::byte>, Extent>
byte_view(View<T, Extent> run) noexcept {
    using Byte = std::conditional_t<std::is_const_v<T>, std::byte const, std::byte>;
    using Untyped = std::conditional_t<std::is_const_v<T>, void const*, void*>;
    Byte* const first = static_cast<Byte*>(static_cast<Untyped>(first_of_(detail::BaseLayerRead{}, run)));
    if constexpr (Extent == dynamic_extent) {
        return View<Byte>{detail::ViewDoor::key(), first, run.size()};
    } else {
        return View<Byte, Extent>{detail::ViewDoor::key(), first};
    }
}

}  // namespace foundation::core
