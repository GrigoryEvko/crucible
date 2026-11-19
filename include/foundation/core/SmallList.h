#pragma once

// The small list of the Region family: up to Inline elements in the object
// itself, and the elements on the heap past them.
//
//   SmallList<T, Inline>   the elements in the order of their appends.  The
//                       first Inline elements live in the list, with no heap
//                       allocation.  The append past them moves each element
//                       to storage on the heap, and each later growth doubles
//                       that storage, up to the maximum of the mint
//
// THE MINT
//   mint_small_list<T, Inline>(maximum)
//                       the one door: an empty list that holds no heap
//                       storage.  No append takes the list past maximum
//                       elements.  A mint allocates nothing, so it takes no
//                       allocation capability.  A maximum past the largest
//                       count of a View becomes that count
//
// THE OPERATIONS
//   append(alloc, value)   puts value after the last element, and gives Unit,
//                       or Full<T> when the list holds maximum elements.  The
//                       allocation capability comes first, so each growth
//                       shows at its call.  An append below Inline elements
//                       allocates nothing
//   take_last()         moves the last element out as an Option<T>, or gives
//                       no value for an empty list
//   size(), is_empty(), clear()
//
// THE FRAME DOORS
//   with_view(body)     calls body with the View of the elements, and gives
//                       the result of body.  The View gives get(position),
//                       the loop and the iteration verbs reverse(),
//                       windows<W>() and chunks<W>()
//   with_mut(std::move(list), body)
//                       the list moves into the frame of the door, body gets
//                       the ViewMut of the elements, and the door gives the
//                       list back (foundation/core/Frame.h)
//
// WHY A FRAME DOOR AND NO view() OR get()
//   An append can move each element: the append past Inline moves them to the
//   heap, and each growth moves them to larger storage.  A move of the list
//   moves the inline elements too.  So a View or a Ref of an element that
//   outlives one call would dangle after the next append or move, as it would
//   for a GrowList.  The list gives no View, no Ref and no cursor out of a
//   frame.  The frame doors keep THE LEND COUNT of foundation/core/Grow.h: an
//   append, a take, a clear, a move and the end of the list while a door lends
//   the elements end the process, in each build.
//
// THE ELEMENT
//   The element is trivially copyable and trivially destructible, and its
//   alignment is at most 8.  The owner core of foundation/core/Owner.h moves
//   the state of an owner by its bytes, and the inline elements live in that
//   state, so a move of the list copies their bytes.  An element with a move
//   of its own, such as a Box or a linear handle, needs an owner core that
//   moves the inline elements one by one.  A list of such elements is a
//   GrowList.  The bound of the alignment keeps the state free of padding.
//
// The state of a list holds no pointer into its own inline elements, because
// a move copies the state to a different address.  The first element is the
// heap storage when the list holds one, and the inline slots else.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Heap.h>
#include <foundation/core/List.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

// The element of a SmallList (THE ELEMENT).
template <class T>
concept SmallListElement =
    OwnedElement<T> && std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && alignof(T) <= 8;

// A count of inline elements: not zero, and not past the largest View.
template <class T, std::size_t Inline>
concept SmallListInline = IndexCount<Inline> && ViewExtent<T, Inline>;

template <class T, std::size_t Inline>
    requires SmallListElement<T> && SmallListInline<T, Inline>
class SmallList;

namespace detail {

// The bytes of the inline slots, rounded up to a word, so the fields after
// them start at a word with no padding.
template <class T, std::size_t Inline>
inline constexpr std::size_t small_slot_bytes_ = (Inline * sizeof(T) + 7) / 8 * 8;

// The inline slots of a SmallList: Inline elements, and the bytes that round
// them up to a word.  The default constructor zeroes the bytes and starts no
// element, so an element with no default constructor passes.  An append
// starts the element of a slot, and no code reads a slot at or past the
// count.  The slots are trivially copyable, so the core copies them with the
// state.
template <class T, std::size_t Inline>
union SmallSlots {
private:
    T elements_[Inline];
    unsigned char bytes_[small_slot_bytes_<T, Inline>];

    // Only the list of the same element and count reaches a slot.
    friend class ::foundation::core::SmallList<T, Inline>;

    // The first slot.
    [[nodiscard]] constexpr T* first() noexcept { return elements_; }
    [[nodiscard]] constexpr T const* first() const noexcept { return elements_; }

public:
    constexpr SmallSlots() noexcept : bytes_{} {}
};

namespace owner {

// The fields of a SmallList.  The heap storage holds capacity elements, or it
// is null while the elements are inline, and then the capacity is the smaller
// of Inline and the maximum.  So the capacity is never above the maximum, and
// an append with room makes one compare.  A moved-from list keeps its
// maximum.  The lend count is that of a GrowList (THE LEND COUNT of
// foundation/core/Grow.h).
template <class T, std::size_t Inline>
struct SmallListState {
    using owner_state_mark = OwnerStateMark;

    SmallSlots<T, Inline> slots{};
    T* heap = nullptr;
    std::size_t count = 0;
    std::size_t capacity = 0;
    std::size_t maximum = 0;
    mutable unsigned char lend_count = 0;
    FixedArray<unsigned char, 7> padding_{};

    // The capacity of the inline slots of a list of at most maximum
    // elements: the smaller of Inline and the maximum.
    [[nodiscard]] static constexpr std::size_t inline_capacity_(Quantity<unit::Element, std::size_t> maximum) noexcept {
        return maximum.raw() < Inline ? maximum.raw() : Inline;
    }

    [[nodiscard]] static constexpr SmallListState emptied(SmallListState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        SmallListState empty{};
        empty.capacity = inline_capacity_(Quantity<unit::Element, std::size_t>{state.maximum});
        empty.maximum = state.maximum;
        return empty;
    }

    // Gives the heap storage back.  Each element is trivially destructible,
    // so no element needs an end.
    static constexpr void release(SmallListState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        if (state.heap != nullptr) {
            heap_release_(static_cast<void*>(state.heap), state.capacity * sizeof(T), alignof(T));
        }
    }
};

}  // namespace owner

}  // namespace detail

// The arguments of mint_small_list: an element and a count of inline slots.
template <class T, std::size_t Inline>
concept CanMintSmallList = SmallListElement<T> && SmallListInline<T, Inline>;

// The mint is the friend of the list, and the list has one private
// constructor.
template <class T, std::size_t Inline>
    requires CanMintSmallList<T, Inline>
[[nodiscard]] constexpr SmallList<T, Inline> mint_small_list(Quantity<unit::Element, std::size_t> maximum) noexcept;

template <class T, std::size_t Inline>
    requires SmallListElement<T> && SmallListInline<T, Inline>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] SmallList final
    : public OwnerCore<detail::owner::SmallListState<T, Inline>> {
    using State = detail::owner::SmallListState<T, Inline>;
    using Core = OwnerCore<State>;

    // The first element: the heap storage, or the inline slots.
    [[nodiscard]] constexpr T* first_() noexcept {
        return this->state_.heap != nullptr ? this->state_.heap : this->state_.slots.first();
    }
    [[nodiscard]] constexpr T const* first_() const noexcept {
        return this->state_.heap != nullptr ? this->state_.heap : this->state_.slots.first();
    }

    // Moves each element into heap storage of the next capacity, and gives
    // the old heap storage back.  The count is the capacity, and it is below
    // the maximum.  The elements are trivially copyable, so one copy of bytes
    // moves them.  Complexity: one allocation and O(size()) bytes.
    [[gnu::cold, gnu::noinline]] void grow_() noexcept {
        State& state = this->state_;
        std::size_t const count = state.count;
        std::size_t const next = detail::next_capacity_(state.capacity, count + 1, state.maximum, sizeof(T));
        void* const target = detail::heap_allocate_(next * sizeof(T), alignof(T));
        if (count != 0) {
            __builtin_memcpy(target, static_cast<void const*>(first_()), count * sizeof(T));
        }
        if (state.heap != nullptr) {
            detail::heap_release_(static_cast<void*>(state.heap), state.capacity * sizeof(T), alignof(T));
        }
        state.heap = static_cast<T*>(target);
        state.capacity = next;
    }

    // The empty list of at most maximum elements.  Only mint_small_list
    // calls it.
    constexpr explicit SmallList(Quantity<unit::Element, std::size_t> maximum) noexcept : Core{} {
        this->state_.capacity = State::inline_capacity_(maximum);
        this->state_.maximum = maximum.raw();
    }

    template <class U, std::size_t OtherInline>
        requires CanMintSmallList<U, OtherInline>
    friend constexpr SmallList<U, OtherInline> mint_small_list(Quantity<unit::Element, std::size_t> maximum) noexcept;

public:
    using element_type = T;

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // list.  A move copies the bytes of the inline elements.
    constexpr SmallList(SmallList&&) noexcept = default;
    constexpr SmallList& operator=(SmallList&&) noexcept = default;

    SmallList(SmallList const&) = delete("a copy of a SmallList past its inline slots allocates, and no allocation "
                                         "capability shows at a copy");
    SmallList& operator=(SmallList const&) = delete("a copy of a SmallList past its inline slots allocates, and no "
                                                    "allocation capability shows at a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return this->state_.count == 0; }

    // Puts value after the last element, or gives Full<T> when the list
    // holds maximum elements.  The capacity is never above the maximum, so an
    // append with room compares the count with the capacity only.  A full
    // storage then compares the count with the maximum, and grows below it.
    // An append below Inline elements allocates nothing.  A lent list ends
    // the process (THE LEND COUNT).
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] constexpr Result<Unit, Full<T>> append(Marker /*allocation*/, T value) & noexcept {
        State& state = this->state_;
        detail::refuse_lent_owner_change_(state.lend_count);
        if (state.count == state.capacity) [[unlikely]] {
            if (state.count == state.maximum) {
                return err(Full<T>{});
            }
            grow_();
        }
        ::new(static_cast<void*>(first_() + state.count)) T(value);
        ++state.count;
        return Unit{};
    }

    // Moves the last element out, or gives no value for an empty list.  One
    // compare.  A lent list ends the process (THE LEND COUNT).
    [[nodiscard]] constexpr Option<T> take_last() & noexcept {
        State& state = this->state_;
        detail::refuse_lent_owner_change_(state.lend_count);
        if (state.count == 0) {
            return Option<T>{none};
        }
        --state.count;
        return Option<T>::some(T{first_()[state.count]});
    }

    // The list then holds no element, and it keeps its storage.  A lent list
    // ends the process (THE LEND COUNT).
    constexpr void clear() & noexcept {
        detail::refuse_lent_owner_change_(this->state_.lend_count);
        this->state_.count = 0;
    }

    // The frame door of the elements (THE FRAME DOORS): body gets the View
    // of the elements, and the list lends them until body returns.  The
    // result of body holds no borrow.  A const list gives a View of const
    // elements.
    template <class Body>
        requires std::is_invocable_v<Body, View<T>> && HoldsNoBorrow<std::invoke_result_t<Body, View<T>>>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, View<T>>
        with_view(Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, View<T>>) {
        [[maybe_unused]] detail::OwnerLend const lend{this->state_.lend_count};
        return static_cast<Body&&>(body)(View<T>{detail::ViewDoor::key(), first_(), this->state_.count});
    }
    template <class Body>
        requires std::is_invocable_v<Body, View<T const>> && HoldsNoBorrow<std::invoke_result_t<Body, View<T const>>>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, View<T const>>
        with_view(Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, View<T const>>) {
        [[maybe_unused]] detail::OwnerLend const lend{this->state_.lend_count};
        return static_cast<Body&&>(body)(View<T const>{detail::ViewDoor::key(), first_(), this->state_.count});
    }

    // A body whose result holds a borrow binds to these overloads, so the
    // refusal gives its reason.
    template <class Body>
        requires std::is_invocable_v<Body, View<T>> && (!HoldsNoBorrow<std::invoke_result_t<Body, View<T>>>)
    void with_view(Body&& body) & = delete("the result of a frame door holds no borrow: the next append can move the "
                                           "elements of the list.  Give a value with no reference and no address");
    template <class Body>
        requires std::is_invocable_v<Body, View<T const>> && (!HoldsNoBorrow<std::invoke_result_t<Body, View<T const>>>)
    void with_view(Body&& body) const& = delete("the result of a frame door holds no borrow: the next append can move "
                                                "the elements of the list.  Give a value with no reference and no "
                                                "address");
    template <class Body>
    void with_view(Body&& body) && = delete("a frame door of a temporary SmallList lends elements that the full "
                                            "expression ends");
    template <class Body>
    void with_view(Body&& body) const&& = delete("a frame door of a temporary SmallList lends elements that the full "
                                                 "expression ends");
};

// The one door of a SmallList: an empty list of at most maximum elements.  It
// allocates nothing.  Complexity: O(Inline) bytes of zero.
template <class T, std::size_t Inline>
    requires CanMintSmallList<T, Inline>
[[nodiscard]] constexpr SmallList<T, Inline> mint_small_list(Quantity<unit::Element, std::size_t> maximum) noexcept {
    std::size_t const largest = max_view_count(sizeof(T));
    std::size_t const wanted = maximum.raw();
    return SmallList<T, Inline>{Quantity<unit::Element, std::size_t>{wanted < largest ? wanted : largest}};
}

// The frame door of a SmallList (foundation/core/Frame.h).  The list moves
// into the frame, body gets the ViewMut of its elements, and the door gives
// the list back: alone for a body that gives void, and after the result in a
// Record for each other body.  The list lends its elements through with_view
// for the time of the body (THE LEND COUNT), and no code outside the frame can
// reach the list in that time.  The pack refuses each explicit argument, so no
// caller names the brand of the frame.  Complexity: two moves of the state,
// two plain steps of the lend count, and the cost of the body.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
         && ::foundation::reflect::IsInstanceOf<Owner, ^^SmallList>
         && FrameBody<Body, typename Owner::element_type, Fresh>
[[nodiscard]] constexpr detail::frame_return_t<
    std::invoke_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept {
    using T = typename Owner::element_type;
    using Given = std::invoke_result_t<Body&&, ViewMut<T, Fresh>>;
    Owner held{static_cast<Owner&&>(owner)};
    if constexpr (std::is_void_v<Given>) {
        held.with_view([&body](View<T> run) noexcept {
            static_cast<Body&&>(body)(ViewMut<T, Fresh>{detail::FrameDoor::key(), run});
        });
        return held;
    } else {
        std::remove_cv_t<Given> result = held.with_view([&body](View<T> run) noexcept -> std::remove_cv_t<Given> {
            return static_cast<Body&&>(body)(ViewMut<T, Fresh>{detail::FrameDoor::key(), run});
        });
        return detail::frame_return_t<Given, Owner>{static_cast<std::remove_cv_t<Given>&&>(result),
                                                    static_cast<Owner&&>(held)};
    }
}

// A body whose result holds a borrow or names the brand of the frame binds to
// this overload, so the refusal gives its reason.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
             && ::foundation::reflect::IsInstanceOf<Owner, ^^SmallList>
             && RefusedFrameBody<Body, typename Owner::element_type, Fresh>
void with_mut(Owner&& owner, Body&& body) = delete(
    "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
    "the frame: give a plain value");

// A named list stays with the caller while the body runs, so the body could
// append to it through a capture and move the elements of the ViewMut.  This
// twin refuses a named list and a const one, which cannot move into the
// frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Owner>, ^^SmallList>
void with_mut(Owner&& owner, Body&& body) = delete(
    "with_mut takes the owner into its frame and gives it back: pass std::move(owner), and keep the owner that the "
    "call returns");

}  // namespace foundation::core
