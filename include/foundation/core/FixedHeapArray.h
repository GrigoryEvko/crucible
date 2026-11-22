#pragma once

// The fixed heap array of the Region family: exactly N elements on the heap,
// at the alignment Align.
//
//   FixedHeapArray<T, N, Align, KeyBrand>
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
// THE KEYED ARRAY
//   FixedHeapArray<T, N, Align, KeyBrand> with a key brand opens its frames
//   only with a LoanKey<KeyBrand> (THE LOAN KEY of foundation/core/Frame.h),
//   as a keyed FixedArray does:
//     key = array.with_mut(std::move(key), body)
//     array.with_mut(key, body)
//     key = array.with_element(std::move(key), index, body)
//     array.with_element(key, index, body)
//   The doors with no key are refused on such an array.  The array is a
//   member of a class, so its key comes from a root door, and the mint takes
//   the brand with no key.  The spelling with no key brand has the brand
//   Unkeyed and the doors above with no key.
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
#include <foundation/Platform.h>
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

template <class T, std::size_t N, std::size_t Align = alignof(T), ::foundation::brand::IsBrand KeyBrand = Unkeyed>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
class [[nodiscard]][[= ::foundation::transfer::by_arguments{}]][[= loan_key_mark_when<IsKeyBrand<KeyBrand>>]]
    FixedHeapArray final {
    HeapArray<T, Align> storage_;

    // An array of a key brand opens its frames only with its LoanKey (THE
    // KEYED ARRAY).  An array of Unkeyed, the default, has the doors with no key.
    static constexpr bool is_keyed_ = IsKeyBrand<KeyBrand>;

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
        requires(!is_keyed_) && std::is_invocable_v<Body, T&> && HoldsNoBorrow<detail::body_result_t<Body, T&>>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body, T&>
    void with_element(Index<N> index, Body&& body) & = delete(
        "a keyed FixedHeapArray lends an element that a body writes only with its LoanKey: call with_element(key, "
        "index, body)");
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
    // the brand.  The door always inlines, so a body that its caller inlines
    // costs no call.  Complexity: O(1), and the cost of body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && (!is_keyed_) && FrameBody<Body, T, Fresh, N>
    [[= ::foundation::linear::borrow_match{}]] CRUCIBLE_INLINE constexpr auto with_mut(Body&& body) & noexcept {
        return static_cast<Body&&>(body)(
            ViewMut<T, Fresh, N>{detail::FrameDoor::key(), View<T, N>{detail::ViewDoor::key(), first_()}});
    }

    // A body whose result holds a borrow or names the brand of the frame
    // binds to this overload, so the refusal gives its reason.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && (!is_keyed_) && RefusedFrameBody<Body, T, Fresh, N>
    void with_mut(Body&& body) & = delete(
        "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
        "the frame: give a plain value");

    // A keyed array opens a frame only with its key, so a body with no key
    // binds to this overload, and the refusal gives its reason.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && std::is_invocable_v<Body&&, ViewMut<T, Fresh, N>>
    void with_mut(Body&& body) & =
        delete("a keyed FixedHeapArray opens a frame only with its LoanKey: call with_mut(std::move(key), body) or "
               "with_mut(key, body)");

    // THE KEYED ARRAY: the frame door with the key of the brand of the array,
    // as the keyed frame door of a FixedArray (foundation/core/Array.h).  The
    // key moves into the door and comes back, alone for a body that gives
    // void, and after the result in a Record for each other body.  The body
    // reaches no key of the brand (THE GATE of foundation/core/Frame.h).  The
    // frame brand is the scope brand of the call, so copy() takes its memcpy
    // and write_each its restrict loop.  Complexity: O(1), and the cost of body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    [[nodiscard]] CRUCIBLE_INLINE constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>,
                                                                   LoanKey<KeyBrand>>
    with_mut(LoanKey<KeyBrand>&& key, Body&& body) & noexcept {
        return detail::give_run_back_<Fresh>(key, View<T, N>{detail::ViewDoor::key(), first_()},
                                             static_cast<Body&&>(body));
    }

    // The same door with a borrow of the key for the call: body gets the
    // ViewMut, the key stays with the caller, and the door gives the result
    // of body.  A root door lends the key of a named brand in this form.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    CRUCIBLE_INLINE constexpr auto with_mut(LoanKey<KeyBrand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(
            ViewMut<T, Fresh, N>{detail::FrameDoor::key(), View<T, N>{detail::ViewDoor::key(), first_()}});
    }

    // A body that reaches a key of the brand of the array binds to these
    // overloads, so the refusal gives its reason.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyReachingFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(
        "a body of a keyed frame reaches a LoanKey of the brand of its owner, so code in the body could open a second "
        "frame of the owner: capture only what the body reads, and keep the key with the caller");
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyReachingFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(
        "a body of a keyed frame reaches a LoanKey of the brand of its owner, so code in the body could open a second "
        "frame of the owner: capture only what the body reads, and keep the key with the caller");

    // THE KEYED ARRAY: the element door with the key, as the keyed element
    // door of a FixedArray.  Complexity: O(1), and the cost of body.
    template <class Body>
        requires is_keyed_ && KeyedElementBody<Body, T, KeyBrand>
    [[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, T&>, LoanKey<KeyBrand>>
    with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & noexcept {
        using Given = detail::body_result_t<Body&&, T&>;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(first_()[index.raw()]);
            return std::move(key);
        } else {
            return detail::frame_return_t<Given, LoanKey<KeyBrand>>{static_cast<Body&&>(body)(first_()[index.raw()]),
                                                                    std::move(key)};
        }
    }
    template <class Body>
        requires is_keyed_ && KeyedElementBody<Body, T, KeyBrand>
    constexpr auto with_element(LoanKey<KeyBrand>& /*key*/, Index<N> index, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
        requires is_keyed_ && std::is_nothrow_invocable_v<Body&&, T&> && (!ReachesNoLoanKey<Body, KeyBrand>)
    void with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & = delete(
        "a body of a keyed element door reaches a LoanKey of the brand of its owner, so code in the body could give a "
        "second reference to the element: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && std::is_nothrow_invocable_v<Body&&, T&> && (!ReachesNoLoanKey<Body, KeyBrand>)
    void with_element(LoanKey<KeyBrand>& key, Index<N> index, Body&& body) & = delete(
        "a body of a keyed element door reaches a LoanKey of the brand of its owner, so code in the body could give a "
        "second reference to the element: capture only what the body reads, and keep the key with the caller");

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
// its holder takes it with no move.  A key brand gives a keyed array with no
// key: its holder gets the key from a root door (THE KEYED ARRAY).
// Complexity: one allocation and N copies.
template <class T, std::size_t N, std::size_t Align = alignof(T), ::foundation::brand::IsBrand KeyBrand = Unkeyed,
          class Marker>
    requires CanMintFixedHeapArray<T, N, Align, Marker>
[[nodiscard]] FixedHeapArray<T, N, Align, KeyBrand> mint_fixed_heap_array(Marker /*allocation*/,
                                                                          T const& fill) noexcept {
    return FixedHeapArray<T, N, Align, KeyBrand>{detail::HeapArrayDoor::key(), fill};
}

}  // namespace foundation::core
