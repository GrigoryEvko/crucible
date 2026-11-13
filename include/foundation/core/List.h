#pragma once

// The fixed list of the Region family: at most N elements inline, with a
// count that changes.
//
//   FixedList<T, N>     at most N elements in the object itself, in the order
//                       of their pushes
//   Full<T>             the error of a push into a full list.  For a linear T
//                       it holds the value, so the caller gets the value back.
//                       For each other T it is empty, and the value ends with
//                       the refusal
//
// THE OPERATIONS
//   size()              the count of elements, at most N
//   push(value)         puts the value after the last element, and gives a Ref
//                       to it, or Full<T> when the list holds N elements: one
//                       compare.  An element does not move while the list
//                       lives and holds it
//   take_last()         moves the last element out as an Option<T>, or gives
//                       no value for an empty list: one compare
//   clear()             ends each element.  Only an element that a holder can
//                       drop has it, because the end of a linear element
//                       would lose what it proves
//   view()              the View<T> of the elements.  A View of a temporary
//                       list is refused
//   for (x : list)      each element, in order
//   get(position)       the element at position as an Option of a Ref, or no
//                       value at or past size(): one compare
//   reverse(), windows<W>() and chunks<W>()
//                       the iteration verbs of the View of the elements.  No
//                       type holds the count of a list, so it has no indices()
//                       and no enumerate()
//
// Each Ref of one call of push() or get() has the fresh brand of that call.
//
// A slot past size() holds no element: no code reads it, and the destructor
// ends only the elements below size().  A list of a trivially copyable
// element is trivially copyable, so the ABI passes a small list in
// registers.  A copy of a list of any other element copies each element.  A
// move gives each element to the target and leaves the source empty.  A list
// of a linear element is linear itself, so each path of its holder consumes
// it.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

// The error of a push into a full owner.  The owner holds no more room, so
// the value of the push did not become an element.  An empty Full ends the
// value with the refusal.
template <class T>
struct Full final {
    [[nodiscard]] friend constexpr bool operator==(Full, Full) noexcept = default;
};

// The Full of a linear value holds the value, so the caller of the push gets
// the value back and consumes it.  take() moves the value out of a Full that
// the caller gives up.  The move operations are those of the value, and a
// linear value has a user-provided move assignment, so the Full is not
// trivially copyable either.
template <class T>
    requires(::foundation::linear::is_linear_type(^^T))
class[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Full<T> final {
    T value_;

public:
    constexpr explicit Full(T value) noexcept : value_{static_cast<T&&>(value)} {}

    constexpr Full(Full&&) noexcept = default;
    constexpr Full& operator=(Full&&) noexcept = default;
    Full(Full const&) = delete("a Full holds a linear value, and a copy of the Full would copy the value");
    Full& operator=(Full const&) = delete("a Full holds a linear value, and a copy of the Full would copy the value");
    constexpr ~Full() = default;

    [[nodiscard]] constexpr T take() && noexcept { return static_cast<T&&>(value_); }
    T take() & = delete("take() of a Full that the caller keeps would leave a moved value in it");
};

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
class FixedList;

namespace detail {

// The slots of a FixedList: N elements, each of which starts its lifetime at
// a push and ends it at a take or at the end of the list.  For an element
// that is trivially default constructible, the union value-initializes each
// slot, so no read of a slot past the count can see an indeterminate value.
// For each other element the union starts no element, so its constructor
// and its destructor touch no slot.  It is trivially copyable and trivially
// destructible when its element is, and the list then copies its bytes.  For
// each other element the list copies, moves and ends each element below its
// count itself.  The count of the list is the tag of the union: no code
// reads a slot at or past it.
template <class T, std::size_t N>
union ListSlots {
private:
    T elements_[N];

    // Only the list of the same element and capacity reaches a slot.
    friend class FixedList<T, N>;

    // The slot at position, which is below N.
    [[nodiscard]] constexpr T* slot(std::size_t position) noexcept { return &elements_[position]; }
    [[nodiscard]] constexpr T const* slot(std::size_t position) const noexcept { return &elements_[position]; }

public:
    constexpr ListSlots() noexcept
        requires std::is_trivially_default_constructible_v<T>
        : elements_{} {}
    constexpr ListSlots() noexcept {}

    constexpr ListSlots(ListSlots const&) noexcept
        requires std::is_trivially_copy_constructible_v<T>
    = default;
    constexpr ListSlots(ListSlots&&) noexcept
        requires std::is_trivially_move_constructible_v<T>
    = default;
    constexpr ListSlots& operator=(ListSlots const&) noexcept
        requires std::is_trivially_copy_assignable_v<T>
    = default;
    constexpr ListSlots& operator=(ListSlots&&) noexcept
        requires std::is_trivially_move_assignable_v<T>
    = default;

    constexpr ~ListSlots()
        requires std::is_trivially_destructible_v<T>
    = default;
    constexpr ~ListSlots() {}
};

// An element whose list copies its bytes.
template <class T>
concept BytewiseListElement = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

}  // namespace detail

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
class[[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
     [[= ::foundation::lifetime::no_start_over_bytes{}]] FixedList final {
    detail::ListSlots<T, N> slots_{};
    detail::IndexWord<N> count_ = 0;

    // The first slot.  A constant evaluation reads no slot of an empty list,
    // because the union starts no element, so it forms no pointer into it.
    [[nodiscard]] constexpr T* first_() noexcept { return slots_.slot(0); }
    [[nodiscard]] constexpr T const* first_() const noexcept { return slots_.slot(0); }

    // The door that ends the element in one slot: a take ends the element
    // that it moved out, and the end of the list ends each element that it
    // holds.  The element gave its value out or the list ends it, so the end
    // loses no value that the program can still use.
    [[= ::foundation::linear::drop_door{}]] static constexpr void end_slot_(T& element) noexcept { element.~T(); }

    // Ends each element from the last.  Complexity: O(size()).
    constexpr void end_each_() noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::size_t position = count_; position != 0; --position) {
                end_slot_(*slots_.slot(position - 1));
            }
        }
        count_ = 0;
    }

    // Starts each element of this empty list as a copy of the element at the
    // same position of source.  Complexity: O(source.size()).
    constexpr void copy_each_(FixedList const& source) noexcept {
        for (std::size_t position = 0; position != source.count_; ++position) {
            ::new(static_cast<void*>(slots_.slot(position))) T(*source.slots_.slot(position));
        }
        count_ = source.count_;
    }

    // Starts each element of this empty list as a move of the element at the
    // same position of source, and ends each element of source.  Complexity:
    // O(source.size()).
    constexpr void move_each_(FixedList& source) noexcept {
        for (std::size_t position = 0; position != source.count_; ++position) {
            ::new(static_cast<void*>(slots_.slot(position))) T(static_cast<T&&>(*source.slots_.slot(position)));
        }
        count_ = source.count_;
        source.end_each_();
    }

public:
    using element_type = T;

    // The empty list.
    constexpr FixedList() noexcept = default;

    constexpr FixedList(FixedList const&) noexcept
        requires detail::BytewiseListElement<T>
    = default;
    constexpr FixedList(FixedList const& other) noexcept
        requires(!detail::BytewiseListElement<T> && std::is_nothrow_copy_constructible_v<T>)
    {
        copy_each_(other);
    }

    constexpr FixedList(FixedList&&) noexcept
        requires detail::BytewiseListElement<T>
    = default;
    constexpr FixedList(FixedList&& other) noexcept
        requires(!detail::BytewiseListElement<T>)
    {
        move_each_(other);
    }

    constexpr FixedList& operator=(FixedList const&) noexcept
        requires detail::BytewiseListElement<T>
    = default;
    constexpr FixedList& operator=(FixedList const& other) noexcept
        requires(!detail::BytewiseListElement<T> && std::is_nothrow_copy_constructible_v<T>)
    {
        if (this != &other) {
            end_each_();
            copy_each_(other);
        }
        return *this;
    }

    constexpr FixedList& operator=(FixedList&&) noexcept
        requires detail::BytewiseListElement<T>
    = default;
    constexpr FixedList& operator=(FixedList&& other) noexcept
        requires(!detail::BytewiseListElement<T>)
    {
        if (this != &other) {
            end_each_();
            move_each_(other);
        }
        return *this;
    }

    constexpr ~FixedList()
        requires std::is_trivially_destructible_v<T>
    = default;
    constexpr ~FixedList() { end_each_(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = count_;
        [[assume(count <= N)]];
        return count;
    }

    // Puts value after the last element, and gives a Ref with the fresh brand
    // of the call, or Full<T> when the list holds N elements.  One compare.
    // The pack refuses each explicit argument, and the result type holds the
    // brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Result<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>, Full<T>>
    push(T value) & noexcept {
        if (count_ == N) [[unlikely]] {
            if constexpr (::foundation::linear::is_linear_type(^^T)) {
                return err(Full<T>{static_cast<T&&>(value)});
            } else {
                return err(Full<T>{});
            }
        }
        T* const element = ::new(static_cast<void*>(slots_.slot(count_))) T(static_cast<T&&>(value));
        ++count_;
        return detail::RefDoor::lend<Fresh>(*element);
    }
    void
    push(T) && = delete("a push into a temporary FixedList gives a Ref that dangles at the end of the full expression");

    // Moves the last element out, or gives no value for an empty list.  One
    // compare.
    [[nodiscard]] constexpr Option<T> take_last() & noexcept {
        if (count_ == 0) {
            return Option<T>{none};
        }
        --count_;
        Option<T> result = Option<T>::some(static_cast<T&&>(*slots_.slot(count_)));
        end_slot_(*slots_.slot(count_));
        return result;
    }

    // Ends each element.  Only an element that a holder can drop has clear():
    // the end of a linear element would lose what it proves.  Complexity:
    // O(size()).
    constexpr void clear() & noexcept
        requires DroppablePayload<T>
    {
        end_each_();
    }

    // The View of the elements.  It borrows this list, and a push or a take
    // ends what it borrows.  The count is at most N, and the first count
    // slots hold the elements, so the View comes through the door of the
    // base layer, with no check and no test of a null pointer.  A constant
    // evaluation of an empty list gives the empty View, because the union
    // starts no element, and the evaluation forms no pointer into it.
    [[nodiscard]] constexpr View<T> view() & noexcept {
        if consteval {
            if (count_ == 0) {
                return View<T>{};
            }
        }
        return View<T>{detail::ViewDoor::key(), first_(), size()};
    }
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        if consteval {
            if (count_ == 0) {
                return View<T const>{};
            }
        }
        return View<T const>{detail::ViewDoor::key(), first_(), size()};
    }
    void view() && = delete("a view of a temporary FixedList points into an object that the full expression ends");
    void view() const&& = delete("a view of a temporary FixedList points into an object that the full expression ends");

    // The loop `for (T& element : list)`.
    [[nodiscard]] constexpr ViewCursor<T> begin() & noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end() & noexcept { return view().end(); }
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void
    begin() const&& = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void
    end() const&& = delete("a cursor of a temporary FixedList points into an object that the full expression ends");

    // The element at position, as an Option of a Ref with the fresh brand of
    // the call, or no value at or past size(): one compare.  The pack refuses
    // each explicit argument, and the result type holds the brand in
    // undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) & noexcept {
        if (position >= size()) {
            return Option<Ref<T, Fresh>>{none};
        }
        return detail::element_at_<Fresh>(first_(), size(), position);
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        if (position >= size()) {
            return Option<Ref<T const, Fresh>>{none};
        }
        return detail::element_at_<Fresh>(first_(), size(), position);
    }
    void get(std::size_t) && = delete("an element of a temporary FixedList dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary FixedList dangles at the end of the full expression");

    // The iteration verbs of the View of the elements.
    [[nodiscard]] constexpr auto reverse() & noexcept { return view().reverse(); }
    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void
    reverse() const&& = delete("a loop over a temporary FixedList points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Width <= N)
    [[nodiscard]] constexpr auto windows() & noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Width <= N)
    [[nodiscard]] constexpr auto windows() const& noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
    void
    windows() const&& = delete("a loop over a temporary FixedList points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Width <= N)
    [[nodiscard]] constexpr auto chunks() & noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent) && (Width <= N)
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary FixedList points into an object that the full expression ends");

    // Two lists are equal when they hold the same count of elements, and each
    // element equals the element at the same position of the other.
    [[nodiscard]] friend constexpr bool operator==(FixedList const& left, FixedList const& right) noexcept
        requires detail::ComparableElement<T>
    {
        return equal(left.view(), right.view());
    }
};

}  // namespace foundation::core
