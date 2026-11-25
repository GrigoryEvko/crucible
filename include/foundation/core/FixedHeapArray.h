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
// Mint
//   mint_fixed_heap_array<T, N, Align>(alloc, fill)
//                       N copies of fill.  The allocation capability comes
//                       first, so the allocation shows at its call
//
// Reads
//   size()              N, a constant
//   view()              the View<T const, N> of the elements.  A View of a
//                       temporary array is refused
//   for (x : array)     each element, in order, as T const&
//   at(index)           the element at an Index<N> or at index<I>(), with no
//                       check: the type of the index proves the bound
//
// Writes
//   array.set(index, value), array.exchange(index, value),
//   array.update(index, body), array.fill(value), array.copy_from(view) and
//   array.copy_window(start, view)
//                       the value doors, as a FixedArray has them
//                       (foundation/core/Array.h)
//   array.with_element(index, body)
//                       body gets the element at an Index<N> in place, as T&
//                       when two threads can write one element at the same
//                       time, such as an Atomic, and as T const& in each
//                       other case, and the door gives the result of body
// The result of each body holds no borrow.  An array with no key opens no
// frame of its run, as a FixedArray with no key opens none (the frame door of
// foundation/core/Array.h).
//
// Keyed array
//   FixedHeapArray<T, N, Align, KeyBrand> with a key brand opens its frames
//   only with a LoanKey<KeyBrand> (the loan key in foundation/core/Frame.h),
//   as a keyed FixedArray does:
//     key = array.with_mut(std::move(key), body)
//     array.with_mut(key, body)
//     key = array.with_element(std::move(key), index, body)
//     array.with_element(key, index, body)
//   body of with_mut gets the ViewMut<T, Fresh, N> of the elements in place,
//   with the scope brand of the frame, and the door writes no byte of the
//   array and copies no element.  The doors with no key are refused on such
//   an array, and so are the value doors.  The key of an array that is a
//   member of a class comes from a root door, and the mint above takes the
//   brand with no key.  The key of a local array comes from the mint of
//   foundation/core/KeyedMint.h:
//     auto [array, key] = mint_keyed_owner(fixed_heap_array_owner<T, N>, alloc, fill)
//   The spelling with no key brand has the brand Unkeyed and the doors above
//   with no key.
//
// Owner
//   The array does not copy and does not move.  The mint gives a prvalue, so
//   the initializer of a member or of a local takes the array with no move.
//   Thus an array holds its storage from its mint to its end, and no door
//   tests a null pointer.  The storage is a HeapArray of N elements that the
//   array holds alone, and its end ends each element and gives the storage
//   back.  An element of a linear type is refused, because an array that does
//   not move cannot give such an element back.
//
// End of the array
//   The end of an array ends its elements, from the last, as the end of a
//   FixedArray does (foundation/core/Array.h).  The count is in the type, so no
//   state tells that an element ended.  The end of the array is the end of its
//   scope, and the kind stale-borrow refuses a later use by name.  Only the code
//   of an element can come in between, and a read of the array by it through a
//   second name reads an ended element.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
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

template <class T, std::size_t N, std::size_t Align, ::foundation::brand::IsBrand KeyBrand>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
class FixedHeapArray;

// The door of a FixedHeapArray to the raw constructors of a View (the doors of
// the owners above this header in foundation/core/Region.h).  The invariant:
// the array builds its HeapArray with N elements at its mint, it neither moves
// nor resizes it, and it holds it to its end.  So the first element starts a
// run of N live elements, and the View of a fixed extent N holds live
// elements only.  FixedHeapArray is the one friend, and a home owner row of
// utils/rule-table.txt keeps this definition in this header.
class FixedHeapArrayCountDoor final : ::foundation::NoObject<FixedHeapArrayCountDoor> {
    template <class T, std::size_t N, std::size_t Align, ::foundation::brand::IsBrand KeyBrand>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
              && DroppablePayload<T>
    friend class FixedHeapArray;

    // The N live elements from first.
    template <std::size_t N, class T>
    [[nodiscard]] static constexpr View<T, N> held_run_(T* first) noexcept {
        return View<T, N>{first};
    }
};

// The door of a FixedHeapArray to the key of the frame doors (the doors of
// the owners above foundation/core/Frame.h).  Its one member is private, and
// FixedHeapArray is its one friend.  The invariant: the array reads the run
// of the HeapArray that it holds, and it lends its own elements in place, in
// the frame of a door of the array, with the scope brand of the call of the
// door.  A home owner row of utils/rule-table.txt keeps this definition in
// this header.
class FixedHeapArrayFrameKeyDoor final : ::foundation::NoObject<FixedHeapArrayFrameKeyDoor> {
    template <class T, std::size_t N, std::size_t Align, ::foundation::brand::IsBrand KeyBrand>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
              && DroppablePayload<T>
    friend class FixedHeapArray;

    // The key of the frame doors, for the run of the array.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

template <class T, std::size_t N, std::size_t Align = alignof(T), ::foundation::brand::IsBrand KeyBrand = Unkeyed>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
class [[nodiscard]][[= ::foundation::transfer::by_arguments{}]][[= loan_key_mark_when<IsKeyBrand<KeyBrand>>]]
    FixedHeapArray final {
    HeapArray<T, Align> storage_;

    // An array of a key brand opens its frames only with its LoanKey (the keyed
    // array).  An array of Unkeyed, the default, opens no frame.
    static constexpr bool is_keyed_ = IsKeyBrand<KeyBrand>;

    // The first element.  The mint gives storage of N elements, and the array
    // does not move, so the storage lives until the end of the array.
    [[nodiscard]] constexpr T* first_() noexcept {
        return first_of_(detail::BaseLayerRead{}, frame_run_(FixedHeapArrayFrameKeyDoor::key_(), storage_));
    }
    [[nodiscard]] constexpr T const* first_() const noexcept {
        return first_of_(detail::BaseLayerRead{}, storage_.view());
    }

public:
    using element_type = T;

    // N copies of fill in new storage at the alignment Align, with the key
    // that only HeapArrayDoor builds.  Only the mint holds the key.
    // Complexity: one allocation and N copies.
    FixedHeapArray(detail::HeapArrayKey const& key, T const& fill) noexcept : storage_{key, N, fill} {}

    // The array promises its N elements to its end, so the storage keeps its
    // address until each element ended (the end of the array).
    constexpr ~FixedHeapArray() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            end_in_place_(FixedHeapArrayFrameKeyDoor::key_(), storage_);
        }
    }

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
        return FixedHeapArrayCountDoor::held_run_<N>(first_());
    }
    void view() && = delete("a view of a temporary FixedHeapArray points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary FixedHeapArray points into storage that the full expression frees");

    // The run of the elements for the base, with the key of a frame, as for
    // a FixedArray.
    [[nodiscard]] friend constexpr View<T, N> frame_run_(detail::FrameKey const& /*key*/,
                                                         FixedHeapArray& array) noexcept {
        return FixedHeapArrayCountDoor::held_run_<N>(array.first_());
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

    // The value doors of one element and of the run, as a FixedArray gives
    // them (foundation/core/Array.h): no body runs while a door writes, so no
    // door takes a key, and a keyed array has none of them.  A window copy
    // always inlines, so an append of a ring costs no call.  Complexity: O(1)
    // for one element, O(N) for the run, and O(source.size()) for a window.
    void set(Index<N> index, T const& value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_copy_assignable_v<T>
    {
        first_()[index.raw()] = value;
    }
    void set(Index<N> index, T&& value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_move_assignable_v<T>
    {
        first_()[index.raw()] = static_cast<T&&>(value);
    }
    [[nodiscard]] T exchange(Index<N> index, T value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>
    {
        T held = static_cast<T&&>(first_()[index.raw()]);
        first_()[index.raw()] = static_cast<T&&>(value);
        return held;
    }
    template <class Body>
        requires(!is_keyed_)
             && UpdatableElement<T> && std::is_invocable_v<Body, T&> && HoldsNoBorrow<detail::body_result_t<Body, T&>>
             && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] auto
        update(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        detail::UpdateFrame<T> frame{first_()[index.raw()]};
        return frame.run(static_cast<Body&&>(body));
    }
    template <class Body>
        requires(!is_keyed_) && CopiedOutElement<T> && (!PutBackElement<T>)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(Index<N> index, Body&& body) & = delete(
        "update of an element with no nothrow assignment ends the element in its slot and starts the copy there, so "
        "the move and the end of the element run no code: give the element a nothrow assignment, or make the array "
        "with mint_keyed_owner and call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void update(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no update door, because another name of it could write an element while a frame "
        "of it is open: call with_element(key, index, body)");
    void fill(T const& value) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        ::foundation::core::fill(FixedHeapArrayCountDoor::held_run_<N>(first_()), value);
    }
    CRUCIBLE_INLINE void copy_from(View<T const, N> source) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        ::foundation::core::copy(FixedHeapArrayCountDoor::held_run_<N>(first_()), source);
    }
    [[nodiscard]] CRUCIBLE_INLINE Result<Unit, LengthMismatch> copy_window(std::size_t start,
                                                                           View<T const> source) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        return detail::copy_into_window_(FixedHeapArrayCountDoor::held_run_<N>(first_()), start, source);
    }
    template <class... Arguments>
        requires is_keyed_
    void set(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no value door, because another name of it could write an element while a frame of "
        "it is open: call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void exchange(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no value door, because another name of it could write an element while a frame of "
        "it is open: call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void fill(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no value door, because another name of it could write the run while a frame of it "
        "is open: call with_mut(key, body)");
    template <class... Arguments>
        requires is_keyed_
    void copy_from(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no value door, because another name of it could write the run while a frame of it "
        "is open: call with_mut(key, body)");
    template <class... Arguments>
        requires is_keyed_
    void copy_window(Arguments&&... arguments) & = delete(
        "a keyed FixedHeapArray has no value door, because another name of it could write the run while a frame of it "
        "is open: call with_mut(key, body)");

    // The frame door of one element: body gets the element at a proven index
    // in place, for the time of the call, and the door gives the result of
    // body, which holds no borrow.  A const array gives a const element.  An
    // array with no key gives an element that body writes only when two
    // threads can write it at the same time, as a FixedArray does.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_keyed_) && ::foundation::transfer::Synchronized<T> && std::is_invocable_v<Body, T&>
             && HoldsNoBorrow<detail::body_result_t<Body, T&>> && ::foundation::linear::HoldsNoLinearValue<Body>
             && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
        requires(!is_keyed_) && ::foundation::transfer::Synchronized<T> && LinearElementBody<Body, T>
    void with_element(Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && (!::foundation::transfer::Synchronized<T>)
                 && std::is_invocable_v<Body, T&> && (!std::is_invocable_v<Body, T const&>)
    void with_element(Index<N> index, Body&& body) & = delete(
        "a FixedHeapArray with no key lends no element that a body writes, because another name of the array could "
        "lend the same element: call set, exchange or update(index, body), or make the array with mint_keyed_owner "
        "and call with_element(key, index, body)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body, T&>
    void with_element(Index<N> index, Body&& body) & = delete(
        "a keyed FixedHeapArray lends an element that a body writes only with its LoanKey: call with_element(key, "
        "index, body)");
    template <class Body>
        requires std::is_invocable_v<Body, T const&> && HoldsNoBorrow<detail::body_result_t<Body, T const&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, T const&>) {
        return static_cast<Body&&>(body)(first_()[index.raw()]);
    }
    template <class Body>
        requires LinearElementBody<Body, T const>
    void with_element(Index<N> index, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
    void with_element(Index<N> index, Body&& body) && =
        delete("a frame door of a temporary FixedHeapArray lends an element that the full expression frees");
    template <class Body>
    void with_element(Index<N> index, Body&& body) const&& =
        delete("a frame door of a temporary FixedHeapArray lends an element that the full expression frees");

    // An array with no key opens no frame, and a keyed array opens a frame
    // only with its key, so a body with no key binds to one of these two
    // overloads, and the refusal gives its reason.
    template <class Body>
        requires(!is_keyed_)
    void with_mut(Body&& body) & = delete(
        "a FixedHeapArray with no key opens no frame, because another name of the array could open a second frame of "
        "the same run: call fill, copy_from or copy_window, or make the array with mint_keyed_owner and call "
        "with_mut(key, body)");
    template <class Body>
        requires is_keyed_
    void with_mut(Body&& body) & =
        delete("a keyed FixedHeapArray opens a frame only with its LoanKey: call with_mut(std::move(key), body) or "
               "with_mut(key, body)");

    // Keyed array: the frame door with the key of the brand of the array,
    // as the keyed frame door of a FixedArray (foundation/core/Array.h): body
    // gets the ViewMut<T, Fresh, N> of the elements in place, so the door
    // copies no element.  The key moves into the door and comes back, alone
    // for a body that gives void, and after the result in a Record for each
    // other body.  The body reaches no key of the brand (the gate in
    // foundation/core/Frame.h).  The frame brand is the scope brand of the
    // call, so copy() takes its memcpy and write_each its restrict loop.  The
    // door always inlines, so a body that its caller inlines costs no call.
    // Complexity: O(1), and the cost of body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    [[nodiscard]] CRUCIBLE_INLINE constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>,
                                                                   LoanKey<KeyBrand>>
    with_mut(LoanKey<KeyBrand>&& key, Body&& body) & noexcept {
        return detail::give_run_back_<Fresh>(FixedHeapArrayFrameKeyDoor::key_(), std::move(key),
                                             FixedHeapArrayCountDoor::held_run_<N>(first_()),
                                             static_cast<Body&&>(body));
    }

    // The same door with a borrow of the key for the call: body gets the
    // ViewMut, the key stays with the caller, and the door gives the result
    // of body.  A root door lends the key of a named brand in this form.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    CRUCIBLE_INLINE constexpr auto with_mut(LoanKey<KeyBrand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(
            ViewMut<T, Fresh, N>{FixedHeapArrayFrameKeyDoor::key_(), FixedHeapArrayCountDoor::held_run_<N>(first_())});
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
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedLinearFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedLinearFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    // The twins of KeyedFrameBody (foundation/core/Array.h), which read no call
    // of the body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameGates<Body, T, KeyBrand>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameGates<Body, T, KeyBrand>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedStoringFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedStoringFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);

    // Keyed array: the element door with the key, as the keyed element
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
        requires is_keyed_ && KeyedLinearElementBody<Body, T, KeyBrand>
    void with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && KeyedLinearElementBody<Body, T, KeyBrand>
    void with_element(LoanKey<KeyBrand>& key, Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

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
// key: its holder gets the key from a root door (the keyed array).
// Complexity: one allocation and N copies.
template <class T, std::size_t N, std::size_t Align = alignof(T), ::foundation::brand::IsBrand KeyBrand = Unkeyed,
          class Marker>
    requires CanMintFixedHeapArray<T, N, Align, Marker>
[[nodiscard]] FixedHeapArray<T, N, Align, KeyBrand> mint_fixed_heap_array(Marker /*allocation*/,
                                                                          T const& fill) noexcept {
    return FixedHeapArray<T, N, Align, KeyBrand>{detail::HeapArrayDoor::key(), fill};
}

// The maker of a keyed FixedHeapArray of N copies of a value, for the mint of
// foundation/core/KeyedMint.h (the loan key of foundation/core/Frame.h):
// auto [array, key] = mint_keyed_owner(fixed_heap_array_owner<T, N>, alloc, fill).
// The maker takes the proof of the brand of the mint, so it builds an array
// only inside a mint.  The allocation capability comes first of the arguments,
// so the allocation shows at the call.  Complexity: one allocation and N
// copies.
template <class T, std::size_t N, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
struct FixedHeapArrayOwner final {
    template <IsKeyBrand Brand, class Marker>
        requires CanMintFixedHeapArray<T, N, Align, Marker>
    [[nodiscard]] FixedHeapArray<T, N, Align, Brand> operator()(MintedBrand<Brand> /*proof*/, Marker /*allocation*/,
                                                                T const& fill) const noexcept {
        return FixedHeapArray<T, N, Align, Brand>{detail::HeapArrayDoor::key(), fill};
    }
};

template <class T, std::size_t N, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && detail::HeapArrayAlignment<T, Align>
          && DroppablePayload<T>
inline constexpr FixedHeapArrayOwner<T, N, Align> fixed_heap_array_owner{};

}  // namespace foundation::core
