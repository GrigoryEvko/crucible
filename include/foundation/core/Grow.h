#pragma once

// The growable owners of the Region family: a list and a text on the heap,
// each with a maximum that its mint states.
//
//   GrowList<T>     the elements on the heap, in the order of their appends
//   GrowText        the characters on the heap
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
//   size(), view(), the loop and clear()
//                       as FixedList gives them.  view() of a GrowText gives a
//                       TextView
//   get(position)       of a GrowList: the element at position as an Option of
//                       a Ref, or no value at or past size(): one compare
//
// The allocation capability comes first in each append, so each growth shows
// at its call.  A growth doubles the capacity, up to the maximum, and an
// exhausted memory ends the process in the allocation door of
// foundation/core/Heap.h.  The first storage of a list holds one cache line
// of elements, and at least one.  The owners have no reserve and no resize:
// a count that is known at the start is a HeapArray.
//
// A growth moves each element into new storage, so append gives no
// reference, and a growth ends each borrow of the owner: each View, each
// cursor, each TextView and each Ref of get().  Take a borrow after the
// last append.  An append of a view of the same text is safe: the growth
// copies the characters before it gives the old storage back.
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
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/List.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>

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

namespace owner {

// The fields of a GrowList.  The storage holds capacity elements, or it is
// null when the capacity is zero, and the first count slots hold the
// elements.  The storage names no type of an element, so an assignment of it
// drops no element: each element ends through end_slot_.  A moved-from list
// keeps its maximum.
template <class T>
struct GrowListState {
    void* storage = nullptr;
    std::size_t count = 0;
    std::size_t capacity = 0;
    std::size_t maximum = 0;

    [[nodiscard]] static constexpr GrowListState emptied(GrowListState const& state) noexcept {
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
        end_each_(state);
        if (state.storage != nullptr) {
            heap_release_(state.storage, state.capacity * sizeof(T), alignof(T));
        }
    }
};

// The fields of a GrowText: the characters, or null when the capacity is
// zero.  A moved-from text keeps its maximum.
struct GrowTextState {
    char* first = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t maximum = 0;

    [[nodiscard]] static constexpr GrowTextState emptied(GrowTextState const& state) noexcept {
        return GrowTextState{nullptr, 0, 0, state.maximum};
    }

    static constexpr void release(GrowTextState const& state) noexcept {
        if (state.first != nullptr) {
            heap_release_(static_cast<void*>(state.first), state.capacity, 1);
        }
    }
};

}  // namespace owner

}  // namespace detail

// The element of a GrowList: an owned element that moves with no throw,
// because a growth moves each element.
template <class T>
concept GrowListElement = OwnedElement<T> && std::is_nothrow_move_constructible_v<T>;

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
    // holds maximum elements.  Two compares, and a growth when the storage is
    // full.  The growth moves each element, so the append gives no
    // reference.
    template <class Marker>
        requires AllocationCapability<Marker>
    [[nodiscard]] Result<Unit, Full<T>> append(Marker /*allocation*/, T value) & noexcept {
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
    // what it proves.  Complexity: O(size()).
    constexpr void clear() & noexcept
        requires DroppablePayload<T>
    {
        end_each_();
    }

    // The View of the elements.  It borrows this list, and the next append
    // can end what it borrows.  The mint bounds the count, and the list holds
    // storage for each count above zero, so the View comes through the door
    // of the base layer, with no check.
    [[nodiscard]] constexpr View<T> view() & noexcept {
        return View<T>{detail::ViewDoor::key(), first_(), this->state_.count};
    }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{detail::ViewDoor::key(), first_(), this->state_.count};
    }
    void view() && = delete("a view of a temporary GrowList points into storage that the full expression frees");
    void view() const&& = delete("a view of a temporary GrowList points into storage that the full expression frees");

    // The loop `for (T& element : list)`.
    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept { return view().end(); }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary GrowList points into storage that the full expression frees");
    void end() && = delete("a cursor of a temporary GrowList points into storage that the full expression frees");
    void
    begin() const&& = delete("a cursor of a temporary GrowList points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary GrowList points into storage that the full expression frees");

    // The element at position, as an Option of a Ref with the fresh brand
    // of the call, or no value at or past size(): one compare.  The pack
    // refuses each explicit argument, and the result type holds the brand in
    // undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) & noexcept {
        return detail::element_at_<Fresh>(first_(), this->state_.count, position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        return detail::element_at_<Fresh>(static_cast<T const*>(first_()), this->state_.count, position);
    }
    void get(std::size_t) && = delete("an element of a temporary GrowList dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary GrowList dangles at the end of the full expression");
};

// The one door of a GrowList: an empty list of at most maximum elements.  It
// allocates nothing.  Complexity: O(1).
template <class T>
    requires GrowListElement<T>
[[nodiscard]] constexpr GrowList<T> mint_grow_list(std::size_t maximum) noexcept {
    std::size_t const largest = max_view_count(sizeof(T));
    return GrowList<T>{maximum < largest ? maximum : largest};
}

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
    // Full<char> when they do not fit below the maximum.  Two compares and
    // one copy.  The characters can lie in the storage of this text, below
    // its count or past it after a clear(), so the copy is a move of bytes.
    [[nodiscard]] Result<Unit, Full<char>> put_(char const* characters, std::size_t count) noexcept {
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

    // The text then holds no character, and it keeps its storage.
    constexpr void clear() & noexcept { state_.size = 0; }

    // The characters of the text.  The TextView borrows this text, and the
    // next append can end what it borrows.  The mint bounds the count at
    // __PTRDIFF_MAX__, so the TextView comes through the door of the base
    // layer, with no check.
    [[nodiscard]] constexpr TextView view() const& noexcept { return detail::text_over_(state_.first, state_.size); }
    TextView
    view() const&& = delete("a view of a temporary GrowText points into storage that the full expression frees");

    // The loop `for (char c : text)` reads each character that the text
    // holds.
    [[nodiscard]] constexpr ViewCursor<char const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<char const> end() const& noexcept { return view().end(); }
    void
    begin() const&& = delete("a cursor of a temporary GrowText points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary GrowText points into storage that the full expression frees");
};

// The one door of a GrowText: an empty text of at most maximum characters.
// It allocates nothing.  Complexity: O(1).
[[nodiscard]] constexpr GrowText mint_grow_text(std::size_t maximum) noexcept {
    std::size_t const largest = max_view_count(1);
    return GrowText{maximum < largest ? maximum : largest};
}

}  // namespace foundation::core
