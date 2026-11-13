#pragma once

// The arrays of the Region family: an owner of exactly N elements, and an
// owner of a count of elements on the heap that its mint fixes.
//
//   FixedArray<T, N>    N elements inline, in the object itself
//   HeapArray<T, Align> count elements on the heap, at the alignment Align
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

    // The View of the elements.  It borrows this array.  The array holds N
    // elements, so the View comes through the door of the base layer, with no
    // check and no test of a null pointer.
    [[nodiscard]] constexpr View<T, N> view() & noexcept { return View<T, N>{detail::ViewDoor::key(), elements_}; }
    [[nodiscard]] constexpr View<T const, N> view() const& noexcept {
        return View<T const, N>{detail::ViewDoor::key(), elements_};
    }
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
    // compare.  The pack refuses each explicit argument, and the result
    // type holds the brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) & noexcept {
        return detail::element_at_<Fresh>(&elements_[0], N, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
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
    // the count and gives storage for each count above zero, so the View
    // comes through the door of the base layer, with no check.  An assume of
    // these two facts on the members gave the loop of the probe
    // heap_array_sum two checks of view_over_ and a spill to the stack.
    [[nodiscard]] constexpr View<T> view() & noexcept { return View<T>{detail::ViewDoor::key(), first_, count_}; }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{detail::ViewDoor::key(), first_, count_};
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
    // refuses each explicit argument, and the result type holds the brand in
    // undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) & noexcept {
        return detail::element_at_<Fresh>(first_, count_, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
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
