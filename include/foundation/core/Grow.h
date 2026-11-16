#pragma once

// The growable owners of the Region family: a list and a text on the heap,
// each with a maximum that its mint states, and the text that a GrowText
// gives after its last append.
//
//   GrowList<T>     the elements on the heap, in the order of their appends
//   GrowText        the characters on the heap
//   HeapText        the characters of a GrowText after its last append, which
//                   no operation changes
//
// THE MINTS
//   mint_grow_list<T>(maximum)   the one door of a GrowList: an empty list
//                       that holds no storage.  No append takes the list
//                       past maximum elements
//   mint_grow_text(maximum)      the one door of a GrowText: an empty text
//                       that holds no storage.  No append takes the text past
//                       maximum characters
// A mint allocates nothing, so it takes no allocation capability.  A maximum
// past the largest count of a View becomes that count.
//
// THE OPERATIONS
//   append(alloc, value)   of a GrowList: puts value after the last element,
//                       and gives Unit, or Full<T> when the list holds maximum
//                       elements.  For a linear T the Full holds the value, so
//                       the caller gets it back
//   append(alloc, text)    of a GrowText: puts the characters of a TextView, of
//                       a View<char const> or one character after the last
//                       one, and gives Unit, or Full<char> when they do not
//                       fit below the maximum.  A refused append changes
//                       nothing
//   append_self(alloc, offset, count)
//                       of a GrowText: puts the count characters of the text
//                       from offset after the last one, as append does.  The
//                       growth copies them before it gives the old storage
//                       back
//   size() and clear()  as FixedList gives them
//   freeze(std::move(text))
//                       of a GrowText: gives the characters to a HeapText with
//                       no copy, and leaves the text empty with its maximum
//
// THE FRAME DOORS
//   with_view(body)     of a GrowList and of a GrowText: calls body with the
//                       View of the elements, or with the TextView of the
//                       characters, and gives the result of body
//   with_element(position, on_some, on_none)
//                       of a GrowList: calls on_some with the element at
//                       position, or on_none at or past size(), and gives the
//                       result of the arm that it calls: one compare
//   with_mut(std::move(list), body)
//                       of a GrowList: the list moves into the frame of the
//                       door, body gets the ViewMut of the elements, and the
//                       door gives the list back (foundation/core/Frame.h)
// A frame door lends the storage of its owner to the body for the time of
// the call (THE LEND COUNT).  The frame doors are the only reads of the
// elements and of the characters, so no borrow of a growable owner outlives
// its frame.
//
// The allocation capability comes first in each append, so each growth shows
// at its call.  A growth doubles the capacity, up to the maximum, and an
// exhausted memory ends the process in the allocation door of
// foundation/core/Heap.h.  The first storage of a list holds one cache line
// of elements, and at least one.  The owners have no reserve and no resize:
// a count that is known at the start is a HeapArray.
//
// A growth moves each element into new storage, so append gives no
// reference, and a growth would end each borrow of the owner.  So the owners
// give no View, no cursor, no TextView and no Ref out of a frame.  A text
// appends a part of itself through append_self, which copies the characters
// before the growth gives the old storage back.
//
// THE LEND COUNT
//   A frame door lends the storage of its owner to a body.  A growth would
//   move that storage, and a clear or the end of the owner would end what it
//   holds.  Each door carries the annotation borrow_match, so the rule
//   borrow-match of the quarantine plugin refuses an append, a clear, a move
//   and each other call of a member that is not const of the owner in the
//   body (utils/tools/quarantine/borrow.cpp).  A function that the body calls
//   can still change the owner, so the state of the owner counts its lends.
//   An append, a clear, a move, a freeze and the end of the owner while the
//   count is not zero end the process, in each build.  An append ends the
//   process also when the storage has room, so the refusal does not depend
//   on the capacity.  The count holds 255 lends, and one more ends the
//   process.
//
//   An owner has one owner thread, and only that thread lends it, so the
//   count is a plain byte, with no atomic step.  A frame door of an owner
//   from a second thread is a race on the count.
//
//   The result of a body holds no borrow (HoldsNoBorrow of Choice.h), so no
//   body gives the View, a Ref or an address out.  The concept refuses each
//   class that holds an address, an owner too, so a body that builds an
//   owner writes it to a variable of the caller.  A body can still copy its
//   View into a variable that it captures by reference, and the next growth
//   then ends what that copy borrows.  No rule of the quarantine plugin
//   refuses that route.  The escape rules of the linear design
//   (misc/12_10_2026_linear.md, section 11) are its planned refusal.
//
// A HeapText holds the characters that a GrowText held at its freeze, and no
// operation of it changes them.  Its view() and its loop then give borrows
// that only its end or a move assignment of it ends, as the View of a
// HeapArray does.  A name that a table keeps for the life of the table is a
// HeapText.
//
// An owner owns its storage alone.  A copy would allocate with no
// capability at its call, so an owner does not copy.  A move gives the
// storage to the target and leaves the source empty, with its maximum.  The
// destructor ends each element and gives the storage back.  A list of a
// linear element is linear itself, so each path of its holder consumes it,
// and it has no clear().
//
// The growth of a text and the arithmetic of a capacity are cold, and their
// bodies are in src/foundation/core/Grow.cpp.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Heap.h>
#include <foundation/core/List.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

namespace detail {

// The capacity of the next storage of a growable owner whose elements have
// element_bytes bytes: two times capacity, at least needed and at most
// maximum.  The first storage holds one cache line of elements, and at least
// one.  needed is at most maximum.  Complexity: O(1).
[[nodiscard]] std::size_t next_capacity_(std::size_t capacity, std::size_t needed, std::size_t maximum,
                                         std::size_t element_bytes) noexcept;

// The fatal exits of THE LEND COUNT.  The calls take no argument, so each
// inlined test of a count adds only a compare and a branch to one call.
[[noreturn]] CRUCIBLE_COLD inline void report_lent_owner_change_() noexcept {
    fatal("an append, a clear, a move, a freeze or the end of a growable owner while a frame door lends its storage");
}

[[noreturn]] CRUCIBLE_COLD inline void report_owner_lend_overflow_() noexcept {
    fatal("more than 255 frame doors lend the storage of one growable owner at one time");
}

// The fatal exit of a window of append_self that does not lie in the text.
[[noreturn]] CRUCIBLE_COLD inline void report_self_window_outside_(std::size_t offset, std::size_t count,
                                                                   std::size_t size) noexcept {
    fatal("append_self got the {} characters from {} of a text of {} characters", count, offset, size);
}

// Ends the process while a frame door lends the storage (THE LEND COUNT).
constexpr void refuse_lent_owner_change_(unsigned char lend_count) noexcept {
    if (lend_count != 0) [[unlikely]] {
        report_lent_owner_change_();
    }
}

// The lend of one frame door, until its destructor (THE LEND COUNT).  The
// owner has one owner thread, so the steps of the count are plain.
class [[nodiscard]] OwnerLend final {
    unsigned char& lend_count_;

public:
    constexpr explicit OwnerLend(unsigned char& lend_count) noexcept : lend_count_{lend_count} {
        if (lend_count_ == 255) [[unlikely]] {
            report_owner_lend_overflow_();
        }
        ++lend_count_;
    }
    constexpr ~OwnerLend() { --lend_count_; }

    OwnerLend(OwnerLend const&) = delete("a lend lives in one frame door");
    OwnerLend& operator=(OwnerLend const&) = delete("a lend lives in one frame door");
};

namespace owner {

// The fields of a GrowList.  The storage holds capacity elements, or it is
// null when the capacity is zero, and the first count slots hold the
// elements.  The storage names no type of an element, so an assignment of it
// drops no element: each element ends through end_slot_.  A moved-from list
// keeps its maximum.  The lend count is mutable, because a frame door of a
// const list lends it too (THE LEND COUNT).  The core empties the state at a
// move and releases it at a move assignment and at the end, so emptied and
// release refuse a lent list.
template <class T>
struct GrowListState {
    void* storage = nullptr;
    std::size_t count = 0;
    std::size_t capacity = 0;
    std::size_t maximum = 0;
    mutable unsigned char lend_count = 0;
    FixedArray<unsigned char, 7> padding_{};

    [[nodiscard]] static constexpr GrowListState emptied(GrowListState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        return GrowListState{nullptr, 0, 0, state.maximum};
    }

    // The door that ends the element in one slot: a growth ends each element
    // that it moved out, and clear() and the end of the list end each
    // element that the list holds.
    [[= ::foundation::linear::drop_door{}]] static constexpr void end_slot_(T& element) noexcept { element.~T(); }

    // Ends each element from the last.  Complexity: O(count).
    static constexpr void end_each_(GrowListState const& state) noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            T* const first = static_cast<T*>(state.storage);
            for (std::size_t position = state.count; position != 0; --position) {
                end_slot_(first[position - 1]);
            }
        }
    }

    // Ends each element, and gives the storage back to the allocation door
    // with the bytes and the alignment of its allocation.
    static constexpr void release(GrowListState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        end_each_(state);
        if (state.storage != nullptr) {
            heap_release_(state.storage, state.capacity * sizeof(T), alignof(T));
        }
    }
};

// The fields of a GrowText: the characters, or null when the capacity is
// zero.  A moved-from text keeps its maximum.  The lend count is that of the
// state of a GrowList.
struct GrowTextState {
    char* first = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t maximum = 0;
    mutable unsigned char lend_count = 0;
    FixedArray<unsigned char, 7> padding_{};

    [[nodiscard]] static constexpr GrowTextState emptied(GrowTextState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        return GrowTextState{nullptr, 0, 0, state.maximum};
    }

    static constexpr void release(GrowTextState const& state) noexcept {
        refuse_lent_owner_change_(state.lend_count);
        if (state.first != nullptr) {
            heap_release_(static_cast<void*>(state.first), state.capacity, 1);
        }
    }
};

// The fields of a HeapText: the characters of a GrowText at its freeze, and
// the capacity of their storage, which the release gives back.  The
// characters are null when the capacity is zero.  The HeapText gives only
// const borrows of them.
struct HeapTextState {
    char* first = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;

    [[nodiscard]] static constexpr HeapTextState emptied(HeapTextState const& /*state*/) noexcept {
        return HeapTextState{};
    }

    static constexpr void release(HeapTextState const& state) noexcept {
        if (state.first != nullptr) {
            heap_release_(static_cast<void*>(state.first), state.capacity, 1);
        }
    }
};

}  // namespace owner

}  // namespace detail

// The element of a GrowList: an owned element that moves with no throw,
// because a growth moves each element.  A pinned element derives from
// foundation::Pinned, because its address is its identity, such as an
// Atomic.  A growth would change that address, so the list refuses such an
// element, also when the element writes a move of its own.  A growable set of
// pinned elements is a GrowList of Box<T>, and a fixed set is a HeapArray
// from mint_heap_array_with.
template <class T>
concept GrowListElement =
    OwnedElement<T> && std::is_nothrow_move_constructible_v<T> && !std::is_base_of_v<::foundation::Pinned<T>, T>;

template <class T>
    requires GrowListElement<T>
class GrowList;

class GrowText;

// The two mints are the friends of their owners, and each owner has one
// private constructor.  The rule specialization-home refuses a
// specialization of the list mint outside this header.
template <class T>
    requires GrowListElement<T>
[[nodiscard]] constexpr GrowList<T> mint_grow_list(std::size_t maximum) noexcept;

[[nodiscard]] constexpr GrowText mint_grow_text(std::size_t maximum) noexcept;

template <class T>
    requires GrowListElement<T>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] GrowList final
    : public detail::OwnerCore<detail::owner::GrowListState<T>> {
    using State = detail::owner::GrowListState<T>;
    using Core = detail::OwnerCore<State>;

    [[nodiscard]] constexpr T* first_() const noexcept { return static_cast<T*>(this->state_.storage); }

    // Ends each element from the last.  Complexity: O(size()).
    constexpr void end_each_() noexcept {
        State::end_each_(this->state_);
        this->state_.count = 0;
    }

    // Moves each element into new storage of the next capacity, ends each
    // moved-out element, and gives the old storage back.  The count is below
    // the maximum.  The capacity is at most the largest count of a View, so
    // the bytes fit in size_t.  Complexity: one allocation and O(size())
    // moves.
    [[gnu::cold, gnu::noinline]] void grow_() noexcept {
        State& state = this->state_;
        std::size_t const count = state.count;
        std::size_t const next = detail::next_capacity_(state.capacity, count + 1, state.maximum, sizeof(T));
        void* const target = detail::heap_allocate_(next * sizeof(T), alignof(T));
        if constexpr (detail::BytewiseListElement<T>) {
            if (count != 0) {
                __builtin_memcpy(target, state.storage, count * sizeof(T));
            }
        } else {
            T* const moved = static_cast<T*>(target);
            for (std::size_t position = 0; position != count; ++position) {
                ::new(static_cast<void*>(moved + position)) T(static_cast<T&&>(first_()[position]));
            }
            end_each_();
        }
        if (state.storage != nullptr) {
            detail::heap_release_(state.storage, state.capacity * sizeof(T), alignof(T));
        }
        state.storage = target;
        state.count = count;
        state.capacity = next;
    }

    // The empty list of at most maximum elements.  Only mint_grow_list calls
    // it.
    constexpr explicit GrowList(std::size_t maximum) noexcept : Core{State{nullptr, 0, 0, maximum}} {}

    template <class U>
        requires GrowListElement<U>
    friend constexpr GrowList<U> mint_grow_list(std::size_t maximum) noexcept;

public:
    using element_type = T;

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // list.  The assignment ends each element that this list holds after it
    // takes the storage and the maximum of other, so other can live in one
    // of those elements.  The assignment drops the elements, so a list of a
    // linear element has none.
    constexpr GrowList(GrowList&&) noexcept = default;
    constexpr GrowList& operator=(GrowList&&) noexcept
        requires DroppablePayload<T>
    = default;
    GrowList& operator=(GrowList&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the GrowList holds, and a linear element must not be dropped: "
             "build a new GrowList instead");

    GrowList(GrowList const&) = delete("a copy of a GrowList allocates, and no allocation capability shows at a copy");
    GrowList&
    operator=(GrowList const&) = delete("a copy of a GrowList allocates, and no allocation capability shows at a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }

    // Puts value after the last element, or gives Full<T> when the list
    // holds maximum elements.  Three compares, and a growth when the storage
    // is full.  The growth moves each element, so the append gives no
    // reference, and a lent list ends the process (THE LEND COUNT).
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<T>> append(Marker /*allocation*/, T value) & noexcept {
        detail::refuse_lent_owner_change_(this->state_.lend_count);
        if (this->state_.count == this->state_.maximum) [[unlikely]] {
            if constexpr (::foundation::linear::is_linear_type(^^T)) {
                return err(Full<T>{static_cast<T&&>(value)});
            } else {
                return err(Full<T>{});
            }
        }
        if (this->state_.count == this->state_.capacity) [[unlikely]] {
            grow_();
        }
        ::new(static_cast<void*>(first_() + this->state_.count)) T(static_cast<T&&>(value));
        ++this->state_.count;
        return Unit{};
    }

    // Ends each element and keeps the storage.  Only an element that a
    // holder can drop has clear(): the end of a linear element would lose
    // what it proves.  A lent list ends the process (THE LEND COUNT).
    // Complexity: O(size()).
    constexpr void clear() & noexcept
        requires DroppablePayload<T>
    {
        detail::refuse_lent_owner_change_(this->state_.lend_count);
        end_each_();
    }

    // The frame door of the elements (THE FRAME DOORS): body gets the View
    // of the elements, and the list lends its storage until body returns.
    // The result of body holds no borrow.  A const list gives a View of
    // const elements.
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
    void with_view(Body&& body) & = delete("the result of a frame door holds no borrow: the next append can end the "
                                           "storage of the list.  Give a value with no reference and no address");
    template <class Body>
        requires std::is_invocable_v<Body, View<T const>> && (!HoldsNoBorrow<std::invoke_result_t<Body, View<T const>>>)
    void with_view(Body&& body) const& = delete("the result of a frame door holds no borrow: the next append can end "
                                                "the storage of the list.  Give a value with no reference and no "
                                                "address");
    template <class Body>
    void with_view(Body&& body) && = delete("a frame door of a temporary GrowList lends storage that the full "
                                            "expression frees");
    template <class Body>
    void with_view(Body&& body) const&& = delete("a frame door of a temporary GrowList lends storage that the full "
                                                 "expression frees");

    // The frame door of one element (THE FRAME DOORS): on_some gets the
    // element at position, and the list lends its storage until on_some
    // returns.  At or past size(), on_none runs, with no lend.  The two arms
    // give the same type, which holds no borrow.  One compare.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T&>, std::invoke_result_t<OnNone>>
              && HoldsNoBorrow<std::invoke_result_t<OnNone>>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone>
        with_element(std::size_t position, OnSome&& on_some,
                     OnNone&& on_none) & noexcept(std::is_nothrow_invocable_v<OnSome, T&>
                                                  && std::is_nothrow_invocable_v<OnNone>) {
        if (position >= this->state_.count) {
            return static_cast<OnNone&&>(on_none)();
        }
        [[maybe_unused]] detail::OwnerLend const lend{this->state_.lend_count};
        return static_cast<OnSome&&>(on_some)(first_()[position]);
    }
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
              && HoldsNoBorrow<std::invoke_result_t<OnNone>>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone>
        with_element(std::size_t position, OnSome&& on_some,
                     OnNone&& on_none) const& noexcept(std::is_nothrow_invocable_v<OnSome, T const&>
                                                       && std::is_nothrow_invocable_v<OnNone>) {
        if (position >= this->state_.count) {
            return static_cast<OnNone&&>(on_none)();
        }
        [[maybe_unused]] detail::OwnerLend const lend{this->state_.lend_count};
        return static_cast<OnSome&&>(on_some)(static_cast<T const*>(first_())[position]);
    }
    template <class OnSome, class OnNone>
    void with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) && =
        delete("a frame door of a temporary GrowList lends storage that the full expression frees");
    template <class OnSome, class OnNone>
    void with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) const&& =
        delete("a frame door of a temporary GrowList lends storage that the full expression frees");
};

// The one door of a GrowList: an empty list of at most maximum elements.  It
// allocates nothing.  Complexity: O(1).
template <class T>
    requires GrowListElement<T>
[[nodiscard]] constexpr GrowList<T> mint_grow_list(std::size_t maximum) noexcept {
    std::size_t const largest = max_view_count(sizeof(T));
    return GrowList<T>{maximum < largest ? maximum : largest};
}

// The frame door of a GrowList (foundation/core/Frame.h).  The list moves
// into the frame, body gets the ViewMut of its elements, and the door gives
// the list back: alone for a body that gives void, and after the result in a
// Record for each other body.  The list lends its storage through with_view
// for the time of the body (THE LEND COUNT), and no code outside the frame
// can reach the list in that time, so no growth moves the storage.  The pack
// refuses each explicit argument, so no caller names the brand of the frame.
// Complexity: two moves of the state, two plain steps of the lend count, and
// the cost of the body.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
         && ::foundation::reflect::IsInstanceOf<Owner, ^^GrowList>
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

// A body whose result holds a borrow or names the brand of the frame binds
// to this overload, so the refusal gives its reason.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
             && ::foundation::reflect::IsInstanceOf<Owner, ^^GrowList>
             && RefusedFrameBody<Body, typename Owner::element_type, Fresh>
void with_mut(Owner&& owner, Body&& body) = delete(
    "the result of a frame door leaves the frame, so it holds no reference and no address, and it names no brand of "
    "the frame: give a plain value");

// A named list stays with the caller while the body runs, so the body could
// append to it through a capture and move the storage of the ViewMut.  This
// twin refuses a named list and a const one, which cannot move into the
// frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Owner>, ^^GrowList>
void with_mut(Owner&& owner, Body&& body) = delete(
    "with_mut takes the owner into its frame and gives it back: pass std::move(owner), and keep the owner that the "
    "call returns");

// The characters that a GrowText held at its freeze.  No operation changes
// them, so a borrow of them ends only at the end or at a move assignment of
// the text.  The one door is freeze(std::move(text)) of a GrowText.
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] HeapText final
    : public detail::OwnerCore<detail::owner::HeapTextState> {
    using State = detail::owner::HeapTextState;
    using Core = detail::OwnerCore<State>;

    // The characters of a frozen GrowText.  Only freeze calls it.
    constexpr explicit HeapText(State const& state) noexcept : Core{state} {}

    friend constexpr auto freeze(GrowText&& text) noexcept -> HeapText;

public:
    // The core of foundation/core/Owner.h gives the moves and the end of a
    // text.
    constexpr HeapText(HeapText&&) noexcept = default;
    constexpr HeapText& operator=(HeapText&&) noexcept = default;

    HeapText(HeapText const&) = delete("a copy of a HeapText allocates, and no allocation capability shows at a copy");
    HeapText&
    operator=(HeapText const&) = delete("a copy of a HeapText allocates, and no allocation capability shows at a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return state_.size; }

    // The characters of the text.  The TextView borrows this text, and no
    // operation of the text ends what it borrows.  The GrowText that gave
    // the characters bounds the count at __PTRDIFF_MAX__, so the TextView
    // comes through the door of the base layer, with no check.
    [[nodiscard]] constexpr TextView view() const& noexcept { return detail::text_over_(state_.first, state_.size); }
    TextView
    view() const&& = delete("a view of a temporary HeapText points into storage that the full expression frees");

    // The loop `for (char c : text)` reads each character that the text
    // holds.
    [[nodiscard]] constexpr ViewCursor<char const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<char const> end() const& noexcept { return view().end(); }
    void
    begin() const&& = delete("a cursor of a temporary HeapText points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary HeapText points into storage that the full expression frees");
};

class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] GrowText final
    : public detail::OwnerCore<detail::owner::GrowTextState> {
    using State = detail::owner::GrowTextState;
    using Core = detail::OwnerCore<State>;

    // Moves the characters into new storage of the next capacity, puts the
    // count characters at characters after them, and gives the old storage
    // back.  The characters can lie in the old storage, so the copy comes
    // before the release.  The count fits below the maximum and not in the
    // storage.  The body is in src/foundation/core/Grow.cpp.  Complexity:
    // one allocation and O(size() + count) copies.
    void grow_and_put_(char const* characters, std::size_t count) noexcept;

    // Puts the count characters at characters after the last one, or gives
    // Full<char> when they do not fit below the maximum.  Three compares and
    // one copy.  The characters can lie in the storage of this text, below
    // its count or past it after a clear(), so the copy is a move of bytes.
    // A lent text ends the process (THE LEND COUNT).
    [[nodiscard]] Result<Unit, Full<char>> put_(char const* characters, std::size_t count) noexcept {
        detail::refuse_lent_owner_change_(state_.lend_count);
        if (count > state_.maximum - state_.size) [[unlikely]] {
            return err(Full<char>{});
        }
        if (count > state_.capacity - state_.size) [[unlikely]] {
            grow_and_put_(characters, count);
            return Unit{};
        }
        if (count != 0) {
            __builtin_memmove(state_.first + state_.size, characters, count);
        }
        state_.size += count;
        return Unit{};
    }

    // The empty text of at most maximum characters.  Only mint_grow_text
    // calls it.
    constexpr explicit GrowText(std::size_t maximum) noexcept : Core{State{nullptr, 0, 0, maximum}} {}

    friend constexpr GrowText mint_grow_text(std::size_t maximum) noexcept;

public:
    // The core of foundation/core/Owner.h gives the moves and the end of a
    // text.  The assignment gives the storage of this text back after it
    // takes the storage and the maximum of other.
    constexpr GrowText(GrowText&&) noexcept = default;
    constexpr GrowText& operator=(GrowText&&) noexcept = default;

    GrowText(GrowText const&) = delete("a copy of a GrowText allocates, and no allocation capability shows at a copy");
    GrowText&
    operator=(GrowText const&) = delete("a copy of a GrowText allocates, and no allocation capability shows at a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return state_.size; }

    // Puts one character, the characters of a text, or the characters of a
    // View after the last one, or gives Full<char> when they do not fit
    // below the maximum.  A growth moves the characters, so each append
    // ends each borrow of this text.
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<char>> append(Marker /*allocation*/, char character) & noexcept {
        return put_(&character, 1);
    }
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<char>> append(Marker /*allocation*/, TextView text) & noexcept {
        return put_(detail::text_first_(text), text.size());
    }
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<char>> append(Marker /*allocation*/, View<char const> characters) & noexcept {
        return put_(first_of_(detail::ViewDoor::key(), characters), characters.size());
    }

    // Puts the count characters of this text from offset after the last
    // one, or gives Full<char> when they do not fit below the maximum.  The
    // door takes the place of the characters and not a TextView, so no
    // borrow of the storage lives across the growth.  The growth copies the
    // characters before it gives the old storage back.  A window that does
    // not lie in the text ends the process.
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<char>> append_self(Marker /*allocation*/, std::size_t offset,
                                                       std::size_t count) & noexcept {
        if (offset > state_.size || count > state_.size - offset) [[unlikely]] {
            detail::report_self_window_outside_(offset, count, state_.size);
        }
        return put_(state_.first + offset, count);
    }

    // The text then holds no character, and it keeps its storage.  A lent
    // text ends the process (THE LEND COUNT).
    constexpr void clear() & noexcept {
        detail::refuse_lent_owner_change_(state_.lend_count);
        state_.size = 0;
    }

    // The frame door of the characters (THE FRAME DOORS): body gets the
    // TextView of the characters, and the text lends its storage until body
    // returns.  The result of body holds no borrow.
    template <class Body>
        requires std::is_invocable_v<Body, TextView> && HoldsNoBorrow<std::invoke_result_t<Body, TextView>>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, TextView>
        with_view(Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, TextView>) {
        [[maybe_unused]] detail::OwnerLend const lend{state_.lend_count};
        return static_cast<Body&&>(body)(detail::text_over_(state_.first, state_.size));
    }

    // A body whose result holds a borrow binds to this overload, so the
    // refusal gives its reason.
    template <class Body>
        requires std::is_invocable_v<Body, TextView> && (!HoldsNoBorrow<std::invoke_result_t<Body, TextView>>)
    void with_view(Body&& body) const& = delete("the result of a frame door holds no borrow: the next append can end "
                                                "the storage of the text.  Give a value with no reference and no "
                                                "address, or freeze the text into a HeapText");
    template <class Body>
    void with_view(Body&& body) const&& = delete("a frame door of a temporary GrowText lends storage that the full "
                                                 "expression frees");

    friend constexpr auto freeze(GrowText&& text) noexcept -> HeapText;
};

// Gives the characters of text to a HeapText with no copy, and leaves text
// empty with its maximum.  A lent text ends the process (THE LEND COUNT).
// Only an rvalue freezes, so the call shows the move.  Complexity: O(1).
[[nodiscard]] constexpr auto freeze(GrowText&& text) noexcept -> HeapText {
    detail::owner::GrowTextState const frozen = text.state_;
    text.state_ = detail::owner::GrowTextState::emptied(frozen);
    return HeapText{detail::owner::HeapTextState{frozen.first, frozen.size, frozen.capacity}};
}

void freeze(GrowText& text) = delete("a freeze gives the characters away: freeze an rvalue, such as std::move(text)");
void freeze(GrowText const& text) = delete("a freeze gives the characters away, so it needs a text that is not const");

// The one door of a GrowText: an empty text of at most maximum characters.
// It allocates nothing.  Complexity: O(1).
[[nodiscard]] constexpr GrowText mint_grow_text(std::size_t maximum) noexcept {
    std::size_t const largest = max_view_count(1);
    return GrowText{maximum < largest ? maximum : largest};
}

}  // namespace foundation::core
