#pragma once

// The fixed heap array of the Region family: exactly N elements on the heap,
// at the alignment Align.
//
//   FixedHeapArray<T, N, Align>
//                       N elements in storage of the allocation door, which
//                       the mint fills.  The count is in the type, as for a
//                       FixedArray, and the elements are on the heap, as for
//                       a HeapArray.  So a large run, such as a ring of a
//                       million records, has a View of a fixed extent, and
//                       the object that holds the array holds only a pointer
//                       and a count
//
// THE MINT
//   mint_fixed_heap_array<T, N, Align>(alloc, fill)
//                       N copies of fill.  The allocation capability comes
//                       first, so the allocation shows at its call
//
// THE READS
//   size()              N, a constant
//   view()              the View<T const, N> of the elements.  A View of a
//                       temporary array is refused
//   for (x : array)     each element, in order, as T const&
//   at(index)           the element at an Index<N> or at index<I>(), with no
//                       check: the type of the index proves the bound
//
// THE WRITES
//   array.with_element(index, body)
//                       body gets the element at an Index<N> in place, as
//                       T&, and the door gives the result of body
//   array.with_mut(body)
//                       body gets the ViewMut<T, Fresh, N> of the elements in
//                       place, with the scope brand of the frame, and the
//                       door gives the result of body.  The door writes no
//                       byte of the array and copies no element
// The result of each body holds no borrow.  The two doors carry the
// annotation borrow_match, as the doors of a FixedArray do, so the rule
// borrow-match refuses a second door of the same array in a body
// (foundation/core/Frame.h, THE TWO FORMS OF AN OWNER).
//
// THE OWNER
//   The array does not copy and does not move.  The mint gives a prvalue, so
//   the initializer of a member or of a local takes the array with no move.
//   Thus an array holds its storage from its mint to its end, and no door
//   tests a null pointer.  The storage is a HeapArray of N elements that the
//   array holds alone, and its end ends each element and gives the storage
//   back.  An element of a linear type is refused, because an array that does
//   not move cannot give such an element back.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Index.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

// The arguments of mint_fixed_heap_array: the arguments of mint_heap_array,
// a count that an Index and a View of a fixed extent can hold, and an
// element that a holder can drop.
template <class T, std::size_t N, std::size_t Align, class Marker>
concept CanMintFixedHeapArray =
    CanMintHeapArray<T, Align, Marker> && IndexCount<N> && ViewExtent<T, N> && DroppablePayload<T>;

template <class T, std::size_t N, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
class [[nodiscard]][[= ::foundation::transfer::by_arguments{}]] FixedHeapArray final {
    HeapArray<T, Align> storage_;

    // The first element.  The mint gives storage of N elements, and the array
    // does not move, so the storage lives until the end of the array.
    [[nodiscard]] constexpr T* first_() noexcept {
        return first_of_(detail::ViewDoor::key(), frame_run_(detail::FrameDoor::key(), storage_));
    }
    [[nodiscard]] constexpr T const* first_() const noexcept {
        return first_of_(detail::ViewDoor::key(), storage_.view());
    }

public:
    using element_type = T;

    // N copies of fill in new storage at the alignment Align, with the key
    // that only HeapArrayDoor builds.  Only the mint holds the key.
    // Complexity: one allocation and N copies.
    FixedHeapArray(detail::HeapArrayKey const& key, T const& fill) noexcept : storage_{key, N, fill} {}

    FixedHeapArray(FixedHeapArray const&) =
        delete("a FixedHeapArray owns its storage alone: a copy would free the storage two times");
    FixedHeapArray& operator=(FixedHeapArray const&) =
        delete("a FixedHeapArray owns its storage alone: a copy would free the storage two times");
    FixedHeapArray(FixedHeapArray&&) = delete(
        "a FixedHeapArray holds its storage from its mint to its end, so no door tests a null pointer: initialize "
        "the holder from the mint");
    FixedHeapArray& operator=(FixedHeapArray&&) = delete(
        "a FixedHeapArray holds its storage from its mint to its end, so no door tests a null pointer: initialize "
        "the holder from the mint");

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    // The View of the const elements.  It borrows this array.  A write takes
    // with_element for one element, or with_mut for the run, so no two Views
    // of one array write the same elements.
    [[nodiscard]] constexpr View<T const, N> view() const& noexcept {
        return View<T const, N>{detail::ViewDoor::key(), first_()};
    }
    void view() && = delete("a view of a temporary FixedHeapArray points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary FixedHeapArray points into storage that the full expression frees");

    // The run of the elements for the base, with the key of a frame, as for
    // a FixedArray.
    [[nodiscard]] friend constexpr View<T, N> frame_run_(detail::FrameKey const& /*key*/,
                                                         FixedHeapArray& array) noexcept {
        return View<T, N>{detail::ViewDoor::key(), array.first_()};
    }

    // The loop `for (T const& element : array)`.
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary FixedHeapArray points into storage that the full expression "
                             "frees");
    void end() && = delete("a cursor of a temporary FixedHeapArray points into storage that the full expression frees");
    void begin() const&& = delete("a cursor of a temporary FixedHeapArray points into storage that the full "
                                  "expression frees");
    void end() const&& = delete("a cursor of a temporary FixedHeapArray points into storage that the full expression "
                                "frees");

    // The element at a proven index, for a read.  index<I>() converts to an
    // Index<N> when I < N.
    [[nodiscard]] constexpr T const& at(Index<N> index) const& noexcept { return first_()[index.raw()]; }
    T const& at(Index<N>) && = delete("an element of a temporary FixedHeapArray dangles at the end of the full "
                                      "expression");
    T const& at(Index<N>) const&& = delete("an element of a temporary FixedHeapArray dangles at the end of the full "
                                           "expression");

    // The frame door of one element: body gets the element at a proven index
    // in place, for the time of the call, and the door gives the result of
    // body, which holds no borrow.  A const array gives a const element.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires std::is_invocable_v<Body, T&> && HoldsNoBorrow<detail::body_result_t<Body, T&>>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
        requires std::is_invocable_v<Body, T const&> && HoldsNoBorrow<detail::body_result_t<Body, T const&>>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, T const&>) {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
    void with_element(Index<N> index, Body&& body) && =
        delete("a frame door of a temporary FixedHeapArray lends an element that the full expression frees");
    template <class Body>
    void with_element(Index<N> index, Body&& body) const&& =
        delete("a frame door of a temporary FixedHeapArray lends an element that the full expression frees");

    // The frame door of the run in place: body gets the ViewMut<T, Fresh, N>
    // of the elements, with the scope brand of the frame, and the door gives
    // the result of body.  The array stays in place, so the door copies no
    // element.  The pack refuses each explicit argument, so no caller names
    // the brand.  Complexity: O(1), and the cost of body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && FrameBody<Body, T, Fresh, N>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto with_mut(Body&& body) & noexcept {
        return static_cast<Body&&>(body)(
            ViewMut<T, Fresh, N>{detail::FrameDoor::key(), View<T, N>{detail::ViewDoor::key(), first_()}});
    }

    // A body whose result holds a borrow or names the brand of the frame
    // binds to this overload, so the refusal gives its reason.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && RefusedFrameBody<Body, T, Fresh, N>
    void with_mut(Body&& body) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
        "the frame: give a plain value");

    template <class Body>
    void with_mut(Body&& body) const& = delete("a const FixedHeapArray lends no writer of its elements: read them "
                                               "through view(), or take a FixedHeapArray that is not const");
    template <class Body>
    void with_mut(Body&& body) && =
        delete("a frame door of a temporary FixedHeapArray lends elements that the full expression frees");
    template <class Body>
    void with_mut(Body&& body) const&& =
        delete("a frame door of a temporary FixedHeapArray lends elements that the full expression frees");
};

// The one door of a FixedHeapArray: N copies of fill at the alignment Align,
// in new storage of the allocation door.  The allocation capability comes
// first, so the allocation shows at its call.  The result is a prvalue, so
// its holder takes it with no move.  Complexity: one allocation and N copies.
template <class T, std::size_t N, std::size_t Align = alignof(T), class Marker>
    requires CanMintFixedHeapArray<T, N, Align, Marker>
[[nodiscard]] FixedHeapArray<T, N, Align> mint_fixed_heap_array(Marker /*allocation*/, T const& fill) noexcept {
    return FixedHeapArray<T, N, Align>{detail::HeapArrayDoor::key(), fill};
}

}  // namespace foundation::core
