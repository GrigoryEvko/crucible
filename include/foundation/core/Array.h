#pragma once

// The arrays of the Region family: an owner of exactly N elements, and an
// owner of a count of elements on the heap that its mint fixes.
//
//   FixedArray<T, N>    N elements inline, in the object itself
//   HeapArray<T, Align> count elements on the heap, at the alignment Align
//
// This header also defines get(position) and enumerate() of a View, and the
// free function zip(first, second), because it includes the Ref and Record
// families, and Region.h does not.
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
// element.
//
// A FixedArray fills each element at its construction.  The default
// constructor value-initializes each element, so an array of integers holds
// zeros, and the constructor with N arguments initializes each element from
// its argument.  A list of fewer than N arguments is refused, so no tail of
// an array is left with a value that no code chose.
//
// THE READS
//   size()              N, a constant
//   view()              the View<T, N> of the elements: the door to a View
//                       for code outside the base.  A View of a temporary
//                       array is refused, because it would dangle at the end
//                       of the full expression
//   for (x : array)     each element, in order
//   at(index)           the element at an Index<N> or at index<I>(), with no
//                       check: the type of the index proves the bound
//   indices()           each Index<N>, in order
//   enumerate()         each element with its Index<N>, in order
//
// A FixedArray has no operator[], no data() and no pointer to its
// elements, so no read takes an index that no check proved.  An index from
// data goes through Index<N>::check(raw), which gives an Option.  fill(view,
// value) and copy(destination, source) of the Region family write the
// elements through view().
//
// A FixedArray copies and moves its elements, and it is trivially copyable
// when its element is, so the ABI passes a small array in registers.
//
// THE HEAP ARRAY
//   mint_heap_array(alloc, count, fill)   the one door: count copies of
//                       fill in storage from the allocation door of
//                       foundation/core/Heap.h.  The allocation capability
//                       comes first, so each allocation shows at its call.
//                       A count of zero allocates nothing
//   size(), view(), the loop, get(position), and the iteration verbs, as
//                       a FixedArray gives them.  No type holds the count of a
//                       HeapArray, so it has no indices() and no enumerate()
//
// A HeapArray owns its storage alone.  A move gives the storage to the
// target and leaves the source empty, and the destructor ends each element
// and gives the storage back.  An empty HeapArray, also the default one,
// holds no storage.  A HeapArray of a linear element is linear itself, so
// each path of its holder consumes it.

#include <foundation/Linear.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Index.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail {

// The body of each get of the Region family, which Region.h declares: the
// element at position of the count elements from first, as an Option of a
// Ref with the brand of the public call, or no value at or past the count.
// first is not null when count is above zero.
template <::foundation::brand::IsBrand Brand, class T>
[[nodiscard]] constexpr auto element_at_(T* first, std::size_t count, std::size_t position) noexcept {
    if (position >= count) [[unlikely]] {
        return Option<Ref<T, Brand>>{none};
    }
    return Option<Ref<T, Brand>>::some(RefDoor::lend<Brand>(first[position]));
}

// The cursors of enumerate() and zip obey the rule of a ViewCursor: each
// cursor knows the end of its loop, a read or a step at the end ends the
// process in each build, and the optimizer removes the two checks in a loop
// that compares the cursor with the end first.  A cursor comes only from its
// range, and the seal refuses std::bit_cast and a lifetime start over bytes.

template <class Element, std::size_t Count, class Brand>
class EnumerateCursor;

template <class First, class Second, class Brand>
class ZipCursor;

// The position of a loop over the elements of a View<Element, Count> with
// their indexes: the first element, and the position of the next element.
// The read compares the position with Count first, so the Index of each
// step is an index of indices(), and the type of the View proves the read
// below its count.  The position is a full word, also when the Index holds
// a 32-bit word, so the loop needs one loop variable: the position and the
// offset of the element at the same time.  Each Ref has the brand of the
// call of enumerate() that made the range.
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
        return {Index<Count>{static_cast<IndexWord<Count>>(at_)}, RefDoor::lend<Brand>(first_[at_])};
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

// The pairs of two Views of one fixed extent.  The type proves the counts
// equal, so the loop needs no compare before its first step.  Each Ref of
// one call has the fresh brand of that call, and the pack refuses each
// explicit argument.
template <class... Refused, class First, class Second, std::size_t Extent,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0 && Extent != dynamic_extent)
[[nodiscard]] constexpr detail::ZipRange<First, Second, Fresh> zip(View<First, Extent> first,
                                                                   View<Second, Extent> second) noexcept {
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
// has the fresh brand of that call.
template <class... Refused, class First, std::size_t FirstExtent, class Second, std::size_t SecondExtent,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0 && (FirstExtent == dynamic_extent || SecondExtent == dynamic_extent))
[[nodiscard]] constexpr Result<detail::ZipRange<First, Second, Fresh>, LengthMismatch>
zip(View<First, FirstExtent> first, View<Second, SecondExtent> second) noexcept {
    if (first.size() != second.size()) {
        return err(LengthMismatch{});
    }
    return detail::ZipRange<First, Second, Fresh>{first, second};
}

// The element of an owner of the Region family: an object type that is not
// an array and not cv-qualified, whose destructor does not throw.  A const
// owner gives const elements.
template <class T>
concept OwnedElement = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                    && std::is_nothrow_destructible_v<T>;

namespace detail {

// The type of the parameter at position Position of the list constructor of
// an array: T at each position.
template <class T, std::size_t Position>
using ArrayParameter = T;

template <class T, std::size_t N, class Positions = std::make_index_sequence<N>>
class ArrayStorage;

// The storage of a FixedArray, and its constructors.  The list constructor
// has exactly N parameters of the type T, so a literal argument converts as
// a function argument does, and no tail of the array is left out.
template <class T, std::size_t N, std::size_t... Positions>
class ArrayStorage<T, N, std::index_sequence<Positions...>> {
protected:
    // Value-initialized for the default constructor.  The list constructor
    // initializes each element itself.
    T elements_[N]{};

public:
    // Each element value-initialized: an integer is zero.
    constexpr ArrayStorage() noexcept
        requires std::is_nothrow_default_constructible_v<T>
    = default;

    // Each element from its argument, in order.  The constructor of one
    // argument is explicit, so a value of T does not convert to an array of
    // one element.
    constexpr explicit(N == 1) ArrayStorage(ArrayParameter<T, Positions>... values) noexcept
        requires std::is_nothrow_move_constructible_v<T>
        : elements_{static_cast<T&&>(values)...} {}
};

}  // namespace detail

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
class FixedArray final : private detail::ArrayStorage<T, N> {
    using Storage = detail::ArrayStorage<T, N>;
    using Storage::elements_;

public:
    using element_type = T;

    // Each element value-initialized: an integer is zero.
    constexpr FixedArray() noexcept
        requires std::is_nothrow_default_constructible_v<T>
    = default;

    // The constructor of N elements.
    using Storage::Storage;

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    // The View of the elements.  It borrows this array.
    [[nodiscard]] constexpr View<T, N> view() & noexcept { return detail::view_over_<N>(&elements_[0]); }
    [[nodiscard]] constexpr View<T const, N> view() const& noexcept { return detail::view_over_<N>(&elements_[0]); }
    void view() && = delete("a view of a temporary FixedArray points into an object that the full expression ends");
    void
    view() const&& = delete("a view of a temporary FixedArray points into an object that the full expression ends");

    // The loop `for (T& element : array)`.  A loop over a temporary binds it
    // to a reference first, and a cursor of an rvalue is refused.
    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept { return view().end(); }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void
    begin() const&& = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void
    end() const&& = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");

    // The element at a proven index.  index<I>() converts to an Index<N>
    // when I < N, so a constant index needs no check either.
    [[nodiscard]] constexpr T& at(Index<N> index) & noexcept { return elements_[index.raw()]; }
    [[nodiscard]] constexpr T const& at(Index<N> index) const& noexcept { return elements_[index.raw()]; }
    T& at(Index<N>) && = delete("an element of a temporary FixedArray dangles at the end of the full expression");
    T const&
        at(Index<N>) const&& = delete("an element of a temporary FixedArray dangles at the end of the full expression");

    // The element at a position that no type proves, as an Option of a
    // Ref with the fresh brand of the call, or no value at or past N: one
    // compare.  The pack refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, Fresh>> get(std::size_t position) & noexcept {
        return detail::element_at_<Fresh>(&elements_[0], N, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, Fresh>> get(std::size_t position) const& noexcept {
        return detail::element_at_<Fresh>(&elements_[0], N, position);
    }
    void get(std::size_t) && = delete("an element of a temporary FixedArray dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary FixedArray dangles at the end of the full expression");

    // Each index of the array, in order: the loop
    // `for (Index<N> index : array.indices())`.
    [[nodiscard]] static constexpr IndexRange<N> indices() noexcept { return IndexRange<N>{}; }

    // The iteration verbs of the View of the elements: each element with
    // its index, each element from the last, each run of Width consecutive
    // elements, and each run of Width elements with the rest.  The loop of a
    // temporary array is refused, because its elements would dangle.
    // Each Ref of one call of enumerate() has the fresh brand of that call,
    // and the pack refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto enumerate() & noexcept {
        return detail::EnumerateRange<T, N, Fresh>{view()};
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto enumerate() const& noexcept {
        return detail::EnumerateRange<T const, N, Fresh>{view()};
    }
    void enumerate() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    [[nodiscard]] constexpr auto reverse() & noexcept { return view().reverse(); }
    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void reverse() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto windows() & noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto windows() const& noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
    void windows() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto chunks() & noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    // Two arrays are equal when each element equals the element at the same
    // position of the other array.
    [[nodiscard]] friend constexpr bool operator==(FixedArray const& left, FixedArray const& right) noexcept
        requires detail::ComparableElement<T>
    {
        return equal(left.view(), right.view());
    }
};

// ── HeapArray ──────────────────────────────────────────────────────────────

namespace detail {

// The alignment of the storage of a HeapArray: a power of two, and at least
// the alignment of the element.
template <class T, std::size_t Align>
concept HeapArrayAlignment = Align != 0 && (Align & (Align - 1)) == 0 && Align >= alignof(T);

// The bytes of the storage of count elements of T, rounded up to the
// alignment.  A count past the largest count of a View, or with bytes that
// do not fit in size_t, ends the process.  Complexity: O(1).
template <class T, std::size_t Align>
[[nodiscard]] constexpr std::size_t heap_array_bytes_(std::size_t count) noexcept {
    if (count > max_view_count(sizeof(T))) [[unlikely]] {
        fatal("a HeapArray of {} elements of {} bytes is longer than the largest View", count, sizeof(T));
    }
    std::size_t const element_bytes =
        checked_mul(count, sizeof(T)).expect("the bytes of the elements of a HeapArray fit in size_t");
    std::size_t const padded =
        checked_add(element_bytes, Align - 1).expect("the bytes of a HeapArray at its alignment fit in size_t");
    return padded & ~(Align - 1);
}

// The door of mint_heap_array, and the key of the constructor of a
// HeapArray.  HeapArrayDoor is the one friend of the key, and only
// mint_heap_array calls the door.  The door and the key hold no template,
// so no explicit specialization of a template reaches the key.  The
// constructor that takes the key allocates the storage and builds each
// element itself.  So a use of namespace detail outside the base gets only a
// HeapArray of new storage, with no allocation capability at its call site.
struct HeapArrayDoor;

class [[nodiscard]] HeapArrayKey final {
    friend struct HeapArrayDoor;

    constexpr HeapArrayKey() noexcept {}
    constexpr HeapArrayKey(HeapArrayKey const&) noexcept {}

public:
    HeapArrayKey& operator=(HeapArrayKey const&) = delete("a key of a HeapArray lives for one construction");
};

struct HeapArrayDoor {
    [[nodiscard]] static constexpr HeapArrayKey key() noexcept { return HeapArrayKey{}; }
};

}  // namespace detail

// The arguments of mint_heap_array: an element that copies with no throw,
// an alignment of the storage, and the allocation capability.
template <class T, std::size_t Align, class Marker>
concept CanMintHeapArray = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                        && std::is_nothrow_copy_constructible_v<T>;

template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]] HeapArray final {
    T* first_ = nullptr;
    std::size_t count_ = 0;

    // Ends each element from the last, and gives the storage back to the
    // allocation door with the bytes and the alignment of the mint.  An
    // empty array holds no storage.  Complexity: O(count).
    constexpr void release_() noexcept {
        if (first_ == nullptr) {
            return;
        }
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::size_t position = count_; position != 0; --position) {
                first_[position - 1].~T();
            }
        }
        detail::heap_release_(static_cast<void*>(first_), detail::heap_array_bytes_<T, Align>(count_), Align);
        first_ = nullptr;
        count_ = 0;
    }

public:
    using element_type = T;

    // The empty array: no element and no storage.
    constexpr HeapArray() noexcept = default;

    // count copies of fill in new storage at the alignment Align, with the
    // key that only HeapArrayDoor builds.  A count of zero gives the empty
    // array and allocates nothing.  The allocation door never gives a null
    // pointer, and a count whose bytes do not fit ends the process before the
    // allocation.  Complexity: one allocation and O(count) copies.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, std::size_t count, T const& fill) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        if (count == 0) {
            return;
        }
        std::size_t const bytes = detail::heap_array_bytes_<T, Align>(count);
        T* const first = static_cast<T*>(detail::heap_allocate_(bytes, Align));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(fill);
        }
        first_ = first;
        count_ = count;
    }

    constexpr HeapArray(HeapArray&& other) noexcept : first_{other.first_}, count_{other.count_} {
        other.first_ = nullptr;
        other.count_ = 0;
    }

    // Ends each element that this array holds, and takes the storage of
    // other.
    constexpr HeapArray& operator=(HeapArray&& other) noexcept {
        if (this != &other) {
            release_();
            first_ = other.first_;
            count_ = other.count_;
            other.first_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    HeapArray(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");
    HeapArray&
    operator=(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");

    constexpr ~HeapArray() { release_(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    // The View of the elements.  It borrows this array.  The mint bounds
    // the count and gives storage for each count above zero, so the door
    // of the View does no test at run time.
    [[nodiscard]] constexpr View<T> view() & noexcept {
        [[assume(count_ <= max_view_count(sizeof(T)))]];
        [[assume((first_ == nullptr) == (count_ == 0))]];
        return detail::view_over_(first_, count_);
    }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        [[assume(count_ <= max_view_count(sizeof(T)))]];
        [[assume((first_ == nullptr) == (count_ == 0))]];
        return detail::view_over_(static_cast<T const*>(first_), count_);
    }
    void view() && = delete("a view of a temporary HeapArray points into storage that the full expression frees");
    void view() const&& = delete("a view of a temporary HeapArray points into storage that the full expression frees");

    // The loop `for (T& element : array)`.
    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept { return view().end(); }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void end() && = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void
    begin() const&& = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");

    // The element at position, as an Option of a Ref with the fresh brand
    // of the call, or no value at or past the count: one compare.  The pack
    // refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, Fresh>> get(std::size_t position) & noexcept {
        return detail::element_at_<Fresh>(first_, count_, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, Fresh>> get(std::size_t position) const& noexcept {
        return detail::element_at_<Fresh>(static_cast<T const*>(first_), count_, position);
    }
    void get(std::size_t) && = delete("an element of a temporary HeapArray dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary HeapArray dangles at the end of the full expression");

    // The iteration verbs of the View of the elements.
    [[nodiscard]] constexpr auto reverse() & noexcept { return view().reverse(); }
    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void
    reverse() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto windows() & noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto windows() const& noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
    void
    windows() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto chunks() & noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    // Two arrays are equal when they hold the same count of elements, and
    // each element equals the element at the same position of the other.
    [[nodiscard]] friend constexpr bool operator==(HeapArray const& left, HeapArray const& right) noexcept
        requires detail::ComparableElement<T>
    {
        return equal(left.view(), right.view());
    }
};

// The one door of a HeapArray: count copies of fill at the alignment Align,
// in new storage of the allocation door.  The allocation capability comes
// first, so each allocation shows at its call.  Complexity: one allocation
// and O(count) copies.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArray<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array(Marker /*allocation*/, std::size_t count, T const& fill) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), count, fill};
}

}  // namespace foundation::core
