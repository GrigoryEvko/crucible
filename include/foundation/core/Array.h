#pragma once

// The arrays of the Region family: an owner of exactly N elements, and an
// owner of a count of elements on the heap that its mint fixes.
//
//   FixedArray<T, N>    N elements inline, in the object itself
//   HeapArray<T, Align> count elements on the heap, at the alignment Align
//   MaskedArray<T, Align>
//                       a power-of-two count of elements on the heap, read
//                       at a masked key with no check
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
//   The five mints put count elements in storage from the allocation door of
//   foundation/core/Heap.h.  The allocation capability comes first in each
//   mint, so each allocation shows at its call.  A count of zero allocates
//   nothing.  Each element holds a value before the mint returns.
//
//   mint_heap_array(alloc, count, fill)
//                       count copies of fill
//   mint_heap_array_with<T>(alloc, count, init)
//                       the element at each position built in place from
//                       init(position).  No element moves, so a pinned
//                       element, such as an Atomic, passes
//   mint_heap_array_copy<T>(alloc, source)
//                       a copy of each element of a View
//   mint_heap_array_zeroed<T>(alloc, count)
//                       count elements with zero in each byte.  Each bit
//                       pattern of the element is a value (BitValid)
//   mint_heap_array_written<T>(alloc, count, writer)
//                       count elements of an implicit-lifetime type that
//                       writer(lease) writes through a WriteLease.  The lease
//                       writes the elements in order and has no read, so no
//                       code reads an element before its write.  A writer
//                       that leaves an element with no write, or that gives
//                       an element past the last one, ends the process before
//                       the array exists
//
//   size(), view(), the loop, get(position), and the iteration verbs, as
//                       a FixedArray gives them.  No type holds the count of a
//                       HeapArray, so it has no indices() and no enumerate()
//
// A HeapArray owns its storage alone.  A move gives the storage to the
// target and leaves the source empty, and the destructor ends each element
// and gives the storage back.  An empty HeapArray, also the default one,
// holds no storage.  A HeapArray of a linear element is linear itself, so
// each path of its holder consumes it.
//
// THE MASKED ARRAY
//   mint_masked_array<T, Align>(alloc, count)   the one door: count
//                       elements from their default constructor, so an
//                       element that does not copy passes.  The count is a
//                       power of two from 1 to 2^31
//   size(), view()      as a HeapArray gives them
//   at_masked(key)      the element at key & (count - 1), with no check
//
// A MaskedArray owns its storage as a HeapArray does.  It has no operator[]
// and no pointer to its elements.
//
// THE FRAME DOOR
//   with_mut(std::move(array), body)   of each of the three arrays: body gets
//                       the ViewMut of the elements, with the brand of the
//                       frame, and the door gives the array back
//                       (foundation/core/Frame.h)
//
// THE LIFETIME START
//   lifetime::start_as_array<T>(storage, count)
//                       the View of count objects of T whose lifetime starts
//                       over the bytes at storage, with no constructor.  T is
//                       an implicit-lifetime type throughout, the concept of
//                       foundation/Lifetime.h.  This header includes that
//                       header and the View, so the door stands here

#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Index.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <cstdint>
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

// The largest count of an array that has the list constructor.  The
// constructor declares one parameter for each element, and the compiler
// builds that declaration for each array type.  An array of 65536 elements
// then costs more than a second of compile time and 100 MB in each unit that
// names it.  No code writes a list of more than 256 values.
inline constexpr std::size_t max_listed_array_count = 256;

template <class T, std::size_t N, class Positions = std::make_index_sequence<(N <= max_listed_array_count) ? N : 0>>
class ArrayStorage;

// The storage of a FixedArray, and its constructors.  The list constructor
// has exactly N parameters of the type T, so a literal argument converts as
// a function argument does, and no tail of the array is left out.  An array
// of more than max_listed_array_count elements has no list constructor.
template <class T, std::size_t N, std::size_t... Positions>
class ArrayStorage<T, N, std::index_sequence<Positions...>> {
protected:
    // Value-initialized for the default constructor.  The list constructor
    // initializes each element itself.
    T elements_[N]{};

    // Each element value-initialized: an integer is zero.  The default
    // constructor of FixedArray holds the constraint.  A constraint here is
    // evaluated when the class of the array is complete.  For an element that
    // is a nested class with default member initializers, inside its
    // enclosing class, that point comes before the compiler reads the
    // initializers.  The trait of the element then gives false, and the
    // compiler keeps that answer for the rest of the unit.  The constructor
    // is protected, so the inherited form of it is no default constructor
    // of a FixedArray whose element has none.
    constexpr ArrayStorage() noexcept = default;

public:
    // Each element from its argument, in order.  The constructor of one
    // argument is explicit, so a value of T does not convert to an array of
    // one element.  The bound comes first in the constraint, so an array
    // past the bound reads no trait of its element.
    constexpr explicit(N == 1) ArrayStorage(ArrayParameter<T, Positions>... values) noexcept
        requires(N <= max_listed_array_count) && std::is_nothrow_move_constructible_v<T>
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
        using Stage = detail::EnumerateStage<T, N, Fresh, N>;
        return Walk<Stage>{Stage{view(), 0}};
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto enumerate() const& noexcept {
        using Stage = detail::EnumerateStage<T const, N, Fresh, N>;
        return Walk<Stage>{Stage{view(), 0}};
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
    // position of the other array, and they order as the first pair of
    // elements that differ.  equal and order of the Protocol family compare
    // the elements, so a float element compares by its bits.
    [[nodiscard]] friend constexpr bool operator==(FixedArray const& left, FixedArray const& right) noexcept
        requires Equatable<T>
    {
        return ::foundation::core::equal(left.view(), right.view());
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(FixedArray const& left,
                                                                    FixedArray const& right) noexcept
        requires Orderable<T>
    {
        // Two arrays of one type hold one count, so the order is the order of
        // the C arrays, with no compare of the counts.
        return ::foundation::core::order(left.elements_, right.elements_);
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
    return align_up(element_bytes, Align).expect("the bytes of a HeapArray at its alignment fit in size_t");
}

// Ends each of the count elements at first, from the last, and gives their
// storage back to the allocation door with the bytes and the alignment of
// the mint.  Null storage holds no element.  Complexity: O(count).
template <class T, std::size_t Align>
constexpr void release_heap_elements_(T* first, std::size_t count) noexcept {
    if (first == nullptr) {
        return;
    }
    if constexpr (!std::is_trivially_destructible_v<T>) {
        for (std::size_t position = count; position != 0; --position) {
            first[position - 1].~T();
        }
    }
    heap_release_(static_cast<void*>(first), heap_array_bytes_<T, Align>(count), Align);
}

namespace owner {

// The fields of a HeapArray: the first element and the count, or null and
// zero when it holds no storage.
template <class T, std::size_t Align>
struct HeapArrayState {
    using owner_state_mark = OwnerStateMark;

    T* first = nullptr;
    std::size_t count = 0;

    [[nodiscard]] static constexpr HeapArrayState emptied(HeapArrayState const& /*state*/) noexcept {
        return HeapArrayState{};
    }

    static constexpr void release(HeapArrayState const& state) noexcept {
        release_heap_elements_<T, Align>(state.first, state.count);
    }
};

// The fields of a MaskedArray: the first element, the count and the mask, or
// null and zeros when it holds no storage.
template <class T, std::size_t Align>
struct MaskedArrayState {
    using owner_state_mark = OwnerStateMark;

    T* first = nullptr;
    std::size_t count = 0;
    std::size_t mask = 0;

    [[nodiscard]] static constexpr MaskedArrayState emptied(MaskedArrayState const& /*state*/) noexcept {
        return MaskedArrayState{};
    }

    static constexpr void release(MaskedArrayState const& state) noexcept {
        release_heap_elements_<T, Align>(state.first, state.count);
    }
};

}  // namespace owner

// The door of the mints of a HeapArray and of mint_masked_array, and the key
// of the constructors of a HeapArray, a MaskedArray and a WriteLease.
// HeapArrayDoor is the one friend of the key, and only the mints call the
// door.  The door and the key hold no template, so no explicit
// specialization of a template reaches the key.  A constructor of an array
// that takes the key allocates the storage and builds each element itself.
// So a use of namespace detail outside the base gets only an array of new
// storage, with no allocation capability at its call site.
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

// The tags of the keyed constructors of a HeapArray, one for each mint
// after mint_heap_array.
struct BuildEachTag final {};
struct CopyTag final {};
struct ZeroTag final {};
struct WriteTag final {};

// The storage of count elements of T at the alignment Align, from the
// allocation door.  count is above zero, and a count whose bytes do not fit
// ends the process before the allocation.  The bytes hold no value.
template <class T, std::size_t Align>
[[nodiscard]] void* allocate_heap_elements_(std::size_t count) noexcept {
    return heap_allocate_(heap_array_bytes_<T, Align>(count), Align);
}

// The fatal exit of a writer of mint_heap_array_written that left elements
// with no write, or that gave elements past the last one.
[[noreturn]] CRUCIBLE_COLD inline void refuse_written_array_(std::size_t count, std::size_t unwritten,
                                                             std::size_t refused) noexcept {
    fatal("the writer of mint_heap_array_written left {} of {} elements with no write, and gave {} elements past "
          "the last one",
          unwritten, count, refused);
}

}  // namespace detail

// The arguments of mint_heap_array and mint_heap_array_copy: an element that
// copies with no throw, an alignment of the storage, and the allocation
// capability.
template <class T, std::size_t Align, class Marker>
concept CanMintHeapArray = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                        && std::is_nothrow_copy_constructible_v<T>;

// The arguments of mint_heap_array_with: an alignment of the storage, the
// allocation capability, and an init that gives a T for each position with
// no throw.  The result is a T and not a type that converts to T, so the
// mint builds each element from it in place, with no copy and no move.  An
// element that does not copy or move, such as an Atomic, passes.
template <class T, std::size_t Align, class Marker, class Init>
concept CanMintHeapArrayWith =
    OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
    && std::is_nothrow_invocable_v<Init&, std::size_t> && std::same_as<std::invoke_result_t<Init&, std::size_t>, T>;

// The arguments of mint_heap_array_zeroed: an element whose lifetime starts
// over its bytes (foundation/Lifetime.h), and of which each bit pattern is a
// value (BitValid), so zero in each byte is a value.  A bool, an enumeration
// with an enumerator, a pointer and a proof do not pass.
template <class T, std::size_t Align, class Marker>
concept CanMintHeapArrayZeroed = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                              && ::foundation::lifetime::ImplicitLifetimeThroughout<T> && BitValid<T>;

// ── WriteLease ─────────────────────────────────────────────────────────────

// The write-only lease of the fresh elements of mint_heap_array_written.
// The lease writes the elements in order, from the first, and each element
// one time.  It has no read: no view(), no get() and no reference to an
// element.  So the writer reads no element that has no write.  The mint
// gives the writer a lease, and it gives the HeapArray only after the
// writer wrote each element.
//
//   put(value)      writes value into the next element
//   put_run(run)    writes the elements of a View into the next elements
//   remaining()     the count of elements with no write
//
// A write past the last element writes nothing, and the mint then ends the
// process when the writer returns.  The lease does not copy and does not
// move, so it stays in the frame of the mint.
template <class T>
    requires RegionCopyElement<T>
class [[nodiscard]] WriteLease final {
    T* next_ = nullptr;
    std::size_t remaining_ = 0;
    std::size_t refused_ = 0;
    // The seal refuses a lease from a byte image, so only the constructor
    // makes one.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The count elements at first, with the key that only HeapArrayDoor
    // builds.  first is not null when count is above zero.
    constexpr WriteLease(detail::HeapArrayKey const& /*key*/, T* first, std::size_t count) noexcept
        : next_{first}, remaining_{count} {}

    WriteLease(WriteLease const&) = delete("a WriteLease lends the elements of one mint, and a copy would write them "
                                           "two times");
    WriteLease& operator=(WriteLease const&) = delete("a WriteLease lends the elements of one mint");

    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return remaining_; }

    // The count of elements that a write past the last element gave.
    [[nodiscard]] constexpr std::size_t refused() const noexcept { return refused_; }

    // Writes value into the next element.  Complexity: O(1).
    void put(T const& value) & noexcept {
        if (remaining_ == 0) [[unlikely]] {
            ++refused_;
            return;
        }
        *next_ = value;
        ++next_;
        --remaining_;
    }

    // Writes each element of run into the next elements, in order: one
    // move of bytes.  run is no part of this lease, because the array does
    // not exist before the mint returns.  A run longer than remaining()
    // writes nothing.  Complexity: O(run.size()).
    void put_run(View<T const> run) & noexcept {
        std::size_t const count = run.size();
        if (count > remaining_) [[unlikely]] {
            refused_ += count;
            return;
        }
        if (count != 0) {
            __builtin_memmove(static_cast<void*>(next_), first_of_(detail::ViewDoor::key(), run), count * sizeof(T));
        }
        next_ += count;
        remaining_ -= count;
    }
};

// The arguments of mint_heap_array_written: an element whose lifetime starts
// over its bytes, and that a move of bytes writes (RegionCopyElement).  The
// writer takes the WriteLease with no throw.
template <class T, std::size_t Align, class Marker, class Writer>
concept CanMintHeapArrayWritten = OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
                               && AllocationCapability<Marker> && ::foundation::lifetime::ImplicitLifetimeThroughout<T>
                               && RegionCopyElement<T> && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>;

// A HeapArray owns its elements alone, so it moves between threads as its
// element does (foundation/Transfer.h).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::transfer::by_arguments{}]] HeapArray final
    : public OwnerCore<detail::owner::HeapArrayState<T, Align>> {
    using State = detail::owner::HeapArrayState<T, Align>;

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
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(fill);
        }
        this->state_ = State{first, count};
    }

    // count elements, each built in place from init(position), in the order
    // of the positions.  The result of init is a T, so the element takes it
    // with no copy and no move.  Complexity: one allocation and O(count)
    // calls of init.
    template <class Init>
        requires std::is_nothrow_invocable_v<Init&, std::size_t>
              && std::same_as<std::invoke_result_t<Init&, std::size_t>, T>
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::BuildEachTag /*tag*/, std::size_t count,
                       Init& init) noexcept {
        if (count == 0) {
            return;
        }
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(init(position));
        }
        this->state_ = State{first, count};
    }

    // A copy of each element of source, in order.  Complexity: one
    // allocation and O(source.size()) copies.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::CopyTag /*tag*/, View<T const> source) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        std::size_t const count = source.size();
        if (count == 0) {
            return;
        }
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        T* target = first;
        for (T const& element : source) {
            ::new(static_cast<void*>(target)) T(element);
            ++target;
        }
        this->state_ = State{first, count};
    }

    // count elements with zero in each byte.  The allocation door gives
    // storage of the allocator of the C library, which starts the lifetime
    // of each element of an implicit-lifetime type with no write.  Each bit
    // pattern of T is a value, so each element holds the value of its zero
    // bytes.  Complexity: one allocation and one write of count * sizeof(T)
    // bytes.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::ZeroTag /*tag*/, std::size_t count) noexcept
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T> && BitValid<T>
    {
        if (count == 0) {
            return;
        }
        void* const storage = detail::allocate_heap_elements_<T, Align>(count);
        __builtin_memset(storage, 0, count * sizeof(T));
        this->state_ = State{static_cast<T*>(storage), count};
    }

    // count elements that writer(lease) writes through a WriteLease.  The
    // allocator starts the lifetime of each element before the writer runs,
    // as for the zeroed array, and the lease writes each element before any
    // code can read it.  The writer runs also for a count of zero, with a
    // lease of no element.  A writer that leaves an element with no write, or
    // that gives an element past the last one, ends the process.  The
    // storage goes back to the allocation door first, and each element is
    // trivially destructible.  The check is a plain branch, so it holds under
    // each contract semantic.  Complexity: one allocation and the writes of
    // the writer.
    template <class Writer>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T> && RegionCopyElement<T>
              && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>
    explicit HeapArray(detail::HeapArrayKey const& key, detail::WriteTag /*tag*/, std::size_t count,
                       Writer& writer) noexcept {
        T* first = nullptr;
        if (count != 0) {
            first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        }
        WriteLease<T> lease{key, first, count};
        writer(lease);
        if (lease.remaining() != 0 || lease.refused() != 0) [[unlikely]] {
            detail::release_heap_elements_<T, Align>(first, count);
            detail::refuse_written_array_(count, lease.remaining(), lease.refused());
        }
        this->state_ = State{first, count};
    }

    // The core of foundation/core/Owner.h gives the moves and the end of an
    // array.  The assignment ends each element that this array holds after
    // it takes other, so other can live in one of those elements.  The
    // assignment drops the elements, so an array of a linear element has
    // none.
    constexpr HeapArray(HeapArray&&) noexcept = default;
    constexpr HeapArray& operator=(HeapArray&&) noexcept
        requires DroppablePayload<T>
    = default;
    HeapArray& operator=(HeapArray&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the HeapArray holds, and a linear element must not be dropped: "
             "build a new HeapArray instead");

    HeapArray(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");
    HeapArray&
    operator=(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }

    // The View of the elements.  It borrows this array.  The mint bounds
    // the count and gives storage for each count above zero, so the View
    // comes through the door of the base layer, with no check.  An assume of
    // these two facts on the members gave the loop of the probe
    // heap_array_sum two checks of view_over_ and a spill to the stack.
    [[nodiscard]] constexpr View<T> view() & noexcept {
        return View<T>{detail::ViewDoor::key(), this->state_.first, this->state_.count};
    }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{detail::ViewDoor::key(), this->state_.first, this->state_.count};
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
        return detail::element_at_<Fresh>(this->state_.first, this->state_.count, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        return detail::element_at_<Fresh>(static_cast<T const*>(this->state_.first), this->state_.count, position);
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
    // They order as the first pair of elements that differ, or as their
    // counts when one is a prefix of the other.  equal and order of the
    // Protocol family compare the elements.
    [[nodiscard]] friend constexpr bool operator==(HeapArray const& left, HeapArray const& right) noexcept
        requires Equatable<T>
    {
        return ::foundation::core::equal(left.view(), right.view());
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(HeapArray const& left,
                                                                    HeapArray const& right) noexcept
        requires Orderable<T>
    {
        return ::foundation::core::order(left.view(), right.view());
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

// count elements at the alignment Align, each built in place from
// init(position), in the order of the positions.  init gives a T with no
// throw, and no element moves after its construction.  Complexity: one
// allocation and O(count) calls of init.
template <class T, std::size_t Align = alignof(T), class Marker, class Init>
    requires CanMintHeapArrayWith<T, Align, Marker, Init>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_with(Marker /*allocation*/, std::size_t count, Init init) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::BuildEachTag{}, count, init};
}

// A copy of each element of source at the alignment Align.  Complexity: one
// allocation and O(source.size()) copies.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArray<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_copy(Marker /*allocation*/,
                                                       std::type_identity_t<View<T const>> source) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::CopyTag{}, source};
}

// count elements at the alignment Align with zero in each byte.  Each bit
// pattern of T is a value, so each element holds a value.  Complexity: one
// allocation and one write of the bytes.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArrayZeroed<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_zeroed(Marker /*allocation*/, std::size_t count) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::ZeroTag{}, count};
}

// count elements at the alignment Align that writer(lease) writes through a
// WriteLease, in order.  The lease has no read, so no element is read before
// its write.  A writer that leaves an element with no write, or that gives an
// element past the last one, ends the process, and the array then never
// exists.  Complexity: one allocation and the writes of the writer.
template <class T, std::size_t Align = alignof(T), class Marker, class Writer>
    requires CanMintHeapArrayWritten<T, Align, Marker, Writer>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_written(Marker /*allocation*/, std::size_t count,
                                                          Writer writer) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::WriteTag{}, count, writer};
}

// ── MaskedArray ────────────────────────────────────────────────────────────

namespace detail {

// The largest count of a MaskedArray.  A slot below the count plus a probe
// step below the count then fits in 32 bits.
inline constexpr std::size_t masked_array_max_count = std::size_t{1} << 31;

// True for a power of two from 1 to masked_array_max_count.
[[nodiscard]] constexpr bool is_masked_array_count_(std::size_t count) noexcept {
    return count != 0 && count <= masked_array_max_count && (count & (count - 1)) == 0;
}

}  // namespace detail

// The arguments of mint_masked_array: an element that its default
// constructor builds with no throw, an alignment of the storage, and the
// allocation capability.  The mint copies no element, so an element that
// does not copy or move, such as a class of Atomic cells, passes.
template <class T, std::size_t Align, class Marker>
concept CanMintMaskedArray = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                          && std::is_nothrow_default_constructible_v<T>;

// An open-addressing table on the heap: a count of elements that is a power
// of two, at the alignment Align.  at_masked(key) reads the element at
// key & (count - 1) with no check, because the mask keeps each key below the
// count.  A probe loop adds its step to the key, and the mask wraps it.
//
// The array owns its storage alone.  A move gives the storage to the target
// and leaves the source empty, and the destructor ends each element and gives
// the storage back.  An empty array, the default one and a moved-from one,
// holds no storage.  at_masked of an empty array ends the process through
// fatal(), so no read goes through the null address.  The array owns its
// elements alone, so it moves between threads as its element does
// (foundation/Transfer.h).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::transfer::by_arguments{}]] MaskedArray final
    : public OwnerCore<detail::owner::MaskedArrayState<T, Align>> {
    using State = detail::owner::MaskedArrayState<T, Align>;

    // The first element.  A default or moved-from array holds no storage.
    // The use-after-move guard refuses a use of a moved-from local that it
    // can see, and each build tests the storage here.
    [[nodiscard]] constexpr T* held_() const noexcept {
        if (this->state_.first == nullptr) [[unlikely]] {
            fatal("an element of a MaskedArray that holds no storage: the array is default or moved from");
        }
        return this->state_.first;
    }

public:
    using element_type = T;

    // The empty array: no element and no storage.
    constexpr MaskedArray() noexcept = default;

    // count elements from their default constructor, in new storage at the
    // alignment Align, with the key that only HeapArrayDoor builds.  The mint
    // states the count as a precondition.  This check holds in each build
    // and under each contract semantic, because a count of zero gives the
    // mask of each bit, and at_masked would then read past the storage.
    // Complexity: one allocation and O(count) constructions.
    explicit MaskedArray(detail::HeapArrayKey const& /*key*/, std::size_t count) noexcept
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (!detail::is_masked_array_count_(count)) [[unlikely]] {
            fatal("a MaskedArray of {} elements: the count must be a power of two from 1 to 2^31", count);
        }
        std::size_t const bytes = detail::heap_array_bytes_<T, Align>(count);
        T* const first = static_cast<T*>(detail::heap_allocate_(bytes, Align));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T();
        }
        this->state_ = State{first, count, count - 1};
    }

    // The core of foundation/core/Owner.h gives the moves and the end of an
    // array.  The assignment ends each element that this array holds after
    // it takes other, so other can live in one of those elements.  The
    // assignment drops the elements, so an array of a linear element has
    // none.
    constexpr MaskedArray(MaskedArray&&) noexcept = default;
    constexpr MaskedArray& operator=(MaskedArray&&) noexcept
        requires DroppablePayload<T>
    = default;
    MaskedArray& operator=(MaskedArray&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the MaskedArray holds, and a linear element must not be dropped: "
             "build a new MaskedArray instead");

    MaskedArray(MaskedArray const&) =
        delete("a MaskedArray owns its storage alone: a copy would free the storage two times");
    MaskedArray& operator=(MaskedArray const&) =
        delete("a MaskedArray owns its storage alone: a copy would free the storage two times");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }

    // The View of the elements.  It borrows this array.  The mint bounds the
    // count and gives storage for each count, so the View comes through the
    // door of the base layer, with no check.
    [[nodiscard]] constexpr View<T> view() & noexcept {
        return View<T>{detail::ViewDoor::key(), this->state_.first, this->state_.count};
    }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{detail::ViewDoor::key(), this->state_.first, this->state_.count};
    }
    void view() && = delete("a view of a temporary MaskedArray points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary MaskedArray points into storage that the full expression frees");

    // The element at key & (count - 1), with no check of the key.  The count
    // is a power of two, so each masked key is below it.  An empty array
    // holds no storage, and each build tests that case here: one test and one
    // branch to a cold call.  Complexity: O(1).
    [[nodiscard]] constexpr T& at_masked(std::uint64_t key) & noexcept { return held_()[key & this->state_.mask]; }
    [[nodiscard]] constexpr T const& at_masked(std::uint64_t key) const& noexcept {
        return held_()[key & this->state_.mask];
    }
    void at_masked(std::uint64_t) && = delete("an element of a temporary MaskedArray dangles at the end of the full "
                                              "expression");
    void at_masked(std::uint64_t) const&& = delete("an element of a temporary MaskedArray dangles at the end of the "
                                                   "full expression");
};

// The one door of a MaskedArray: count elements from their default
// constructor, at the alignment Align, in new storage of the allocation
// door.  The count is a power of two from 1 to 2^31.  The allocation
// capability comes first, so each allocation shows at its call.  The
// allocation door ends the process when the memory is exhausted, as it does
// for mint_heap_array.  Complexity: one allocation and O(count) constructions.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintMaskedArray<T, Align, Marker>
[[nodiscard]] MaskedArray<T, Align> mint_masked_array(Marker /*allocation*/, std::size_t count) noexcept {
    // Not CRUCIBLE_PRE: its assumption under the ignore semantic could delete the check of the constructor.
    contract_assert(detail::is_masked_array_count_(count));
    return MaskedArray<T, Align>{detail::HeapArrayDoor::key(), count};
}

// ── The frame doors ────────────────────────────────────────────────────────

// The arrays that a frame door takes (foundation/core/Frame.h).  Each owns
// its elements alone, and a move gives them to the target: a HeapArray and a
// MaskedArray give their storage, and a FixedArray copies its elements.
template <class Owner>
concept ArrayFrameOwner = ::foundation::reflect::IsInstanceOfAny<Owner, ^^FixedArray, ^^HeapArray, ^^MaskedArray>;

namespace detail {

// The extent of the run of an array in a frame door: the extent of its
// view().  A FixedArray of N elements gives N, and each other array gives the
// dynamic extent.
template <class Owner>
inline constexpr std::size_t frame_extent_v = decltype(std::declval<Owner&>().view())::extent;

}  // namespace detail

// The frame door of an array.  The array moves into the frame, body gets the
// ViewMut of its elements, and the door gives the array back: alone for a
// body that gives void, and after the result in a Record for each other body.
// The ViewMut of a FixedArray has the fixed extent N.  The pack refuses each
// explicit argument, so no caller names the brand of the frame.  A HeapArray
// and a MaskedArray move a pointer and a count in and out.  A FixedArray
// copies its N elements in and out, and the optimizer removes the two copies
// only for a small array.  Complexity: O(1) for a heap array, O(N) for a
// FixedArray, and the cost of the body.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
         && ArrayFrameOwner<Owner>
         && FrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
[[nodiscard]] constexpr detail::frame_return_t<
    std::invoke_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept {
    Owner held{static_cast<Owner&&>(owner)};
    View<typename Owner::element_type, detail::frame_extent_v<Owner>> const run = held.view();
    return detail::give_run_back_<Fresh>(held, run, static_cast<Body&&>(body));
}

// A body whose result holds a borrow or names the brand of the frame binds
// to this overload, so the refusal gives its reason.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
             && ArrayFrameOwner<Owner>
             && RefusedFrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
void with_mut(Owner&& owner, Body&& body) = delete(
    "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
    "the frame: give a plain value");

// A named array stays with the caller while the body runs, so the body could
// read or write it beside the ViewMut through a capture.  This twin refuses
// a named array and a const one, which cannot move into the frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ArrayFrameOwner<std::remove_cvref_t<Owner>>
void with_mut(Owner&& owner, Body&& body) = delete(
    "with_mut takes the owner into its frame and gives it back: pass std::move(owner), and keep the owner that the "
    "call returns");

}  // namespace foundation::core

namespace foundation::lifetime {

// Starts the lifetime of count objects of type T over the storage and
// returns them as a View of the base.  The storage must hold count * sizeof(T)
// bytes aligned for T.  The element of the View is const when the storage or
// T is const.  A single object is a View of one.
//
// The function gives the guarantee of std::start_lifetime_as_array
// ([obj.lifetime], P2590R2).  Each object that it starts has an
// implicit-lifetime type ([basic.types.general]), and the object
// representation of each object is the bytes that the storage holds before
// the call.  The function uses the empty asm statement that libstdc++ 16
// uses for that function (bits/stl_construct.h).  The statement reads and
// writes the bytes of the region, and it returns the pointer.  GCC then
// keeps each store to the bytes before the statement.  It keeps each access
// through the result after the statement.  The type of an earlier object in
// the bytes then does not apply to the new objects.  The statement writes no
// byte and emits no instruction.
//
// The operand has the type of an array of unknown bound of unsigned char.
// GCC reads that type as an access of unknown length (the GCC manual,
// "Extended Asm"), so the operand needs no variable length array.  The bit
// cast removes a const of the storage only for the operand, as the
// const_cast of libstdc++ does.
//
// A View holds at most max_view_count elements, and a pointer that is not
// null when it holds an element.  So the function ends the process for a
// larger count and for a null storage with a count, in each build and under
// each contract semantic.  The two checks read the storage before the asm
// statement, because the statement hides the pointer from the optimizer.  The
// View then comes through the door of the base layer, with no second check.
//
// Volatile storage is refused, because a View holds no volatile element.  A
// caller that reads through volatile converts the address of an element to a
// pointer to volatile, which is an implicit conversion.  An array type T is
// refused for the same reason: a caller starts the lifetime of its elements.
template <ImplicitLifetimeThroughout T, typename Storage>
    requires(!std::is_volatile_v<Storage> && ::foundation::core::ViewElement<element_for_storage_t<Storage, T>>)
[[nodiscard]] ::foundation::core::View<element_for_storage_t<Storage, T>> start_as_array(Storage* storage,
                                                                                         std::size_t count) noexcept {
    using Element = element_for_storage_t<Storage, T>;
    using Untyped = std::conditional_t<std::is_const_v<Storage>, const void, void>;
    if (count > ::foundation::core::max_view_count(sizeof(Element))) [[unlikely]] {
        ::foundation::core::fatal("start_as_array got a count past the largest count of a View");
    }
    if (storage == nullptr && count != 0) [[unlikely]] {
        ::foundation::core::fatal("start_as_array got a null storage with a count");
    }
    Element* first = static_cast<Element*>(static_cast<Untyped*>(storage));
    if (count != 0) {
        auto* const region = __builtin_bit_cast(unsigned char (*)[], static_cast<Untyped*>(storage));
        __asm__ __volatile__("" : "+r"(first), "=m"(*region) : "m"(*region));
    }
    return ::foundation::core::View<Element>{::foundation::core::detail::ViewDoor::key(), first, count};
}

}  // namespace foundation::lifetime
