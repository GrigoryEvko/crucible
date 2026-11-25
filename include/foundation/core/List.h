#pragma once

// The fixed list of the Region family: at most N elements inline, with a
// count that changes.
//
//   FixedList<T, N>     at most N elements in the object itself, in the order
//                       of their appends
//   Full<T>             the error of an append into a full list
//                       (foundation/core/Full.h).  For a linear T it holds
//                       the value, so the caller gets the value back.  For
//                       each other T it is empty, and the value ends with the
//                       refusal
//
// Operations
//   size()              the count of elements, at most N
//   append(value)       puts the value after the last element, and gives the
//                       Index<N> of the element, or Full<T> when the list holds
//                       N elements: one compare
//   insert(index, value)
//                       puts the value before the element at the index, or
//                       after the last element at size(), and moves each later
//                       element one place up.  It gives Unit, or Full<T>
//   set(index, value)   puts the value in the place of the element at the index
//                       and then ends the old element
//   exchange(index, value)
//                       as set, and gives the old element back
//   update(index, body) gives body a copy of the element at the index, assigns
//                       the copy back, and gives the result of body
//   remove(index)       moves the element at the index out as an Option<T> and
//                       moves each later element one place down, or gives no
//                       value at or past size()
//   take_last()         moves the last element out as an Option<T>, or gives
//                       no value for an empty list: one compare
//   std::move(list).drain(body)
//                       gives each element to body by value, in order, and ends
//                       the list
//   std::move(list).expect_empty(why)
//                       ends an empty list, and ends the process with why
//                       when the list still holds an element.  A list of a
//                       linear element ends here
//   clear()             ends each element.  Only an element that a holder can
//                       drop has it, because the end of a linear element
//                       would lose what it proves
//   view()              the View<T const> of the elements.  A View of a
//                       temporary list is refused
//   for (x : list)      each element for a read, in order
//   get(position)       the element at position for a read, as an Option of a
//                       Ref, or no value at or past size(): one compare
//   reverse(), windows<W>() and chunks<W>()
//                       the iteration verbs of the View of the elements.  No
//                       type holds the count of a list, so it has no indices()
//                       and no enumerate()
//   with_mut(std::move(list), body)
//                       the frame door: body gets the ViewMut of the elements,
//                       and the door gives the list back
//                       (foundation/core/Frame.h)
//
// The Ref of one call of get() has the fresh brand of that call.
//
// Writers
//   A door that lends a written element in place would give two writers of one
//   element: another name of the list, such as a capture, a helper or a Ref,
//   calls the door again, or ends the element that the first writer holds.  So
//   a list lends no written element in place, and update gives the body a copy
//   of the element.  A body that writes or takes many elements takes the list
//   by move: with_mut and drain hold the list in their frame, so no other name
//   of the list has an element.  An index at or past size() ends the process
//   at set, exchange and update, because no type holds the count of a list.
//
// Code of the caller in a value door
//   An element can reach its list, through a static list, a Ref or a guard that
//   takes itself out of a registry.  So a value door runs the code of an
//   element only while the list holds a live element in each slot that it
//   counts.  This code is:
//     append           the move of the value, before the count takes it
//     insert, remove   the move and the assignment of each later element, and
//                      in remove the end of the last one, after the count
//                      drops it.  A later element is moved from in between
//     set, exchange    the move and the assignment.  The old element ends
//                      after the new one is in the slot
//     update           the copy, the body and the assignment of the copy
//     take_last        the move, and the end of the moved-from element, after
//                      the count drops it
//     clear, the assignment and the end of a list
//                      the end of each element, after the count drops it
//   An element with no nothrow assignment, or a linear one, frees a slot for a
//   moment in insert, remove, set and exchange.  Then the list counts only the
//   elements before the free slot.
//   A door ends the process when the code of an element changes the count of
//   the list while the door moves or assigns an element.  It also does so when
//   a destructor makes the count larger, because an append or an insert from a
//   destructor starts an element in the slot whose destructor still runs.  A
//   destructor can clear the list, or set, take or remove an element of it.
//   The copy of update holds no slot, so a body that ends the element through a
//   second name ends the process when the door puts the copy back.
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
#include <foundation/NoObject.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Full.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
class FixedList;

// The door of a FixedList to the raw constructors of a View (the doors of the
// owners above this header in foundation/core/Region.h).  The invariant: a list
// starts the element of a slot at an append and ends it at a take or at its
// end, so its first count slots hold live elements and no other slot does.
// So the View of the first count slots holds live elements only.  FixedList is
// the one friend, and a home owner row of utils/rule-table.txt keeps this
// definition in this header.
class FixedListSlotsDoor final : ::foundation::NoObject<FixedListSlotsDoor> {
    template <class T, std::size_t N>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
    friend class FixedList;

    // The count live elements from the first slot.
    template <class T>
    [[nodiscard]] static constexpr View<T> live_slots_(T* first, std::size_t count) noexcept {
        return View<T>{first, count};
    }
};

// The door of a FixedList to the body of each get of a run (RegionRefDoor of
// foundation/core/Region.h).  Its member is private, and FixedList is its one
// friend.  The invariant is the invariant of FixedListSlotsDoor: the first
// count slots hold live elements and no other slot does.  So each element
// below the count is live.  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class ListRefDoor final : ::foundation::NoObject<ListRefDoor> {
    template <class T, std::size_t N>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
    friend class FixedList;

    // The element at position of the count live elements from the first slot,
    // as an Option of a Ref of the brand, or no value at or past the count.
    template <::foundation::brand::IsBrand Brand, class T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr auto element_at_(T* first, std::size_t count,
                                                                    std::size_t position) noexcept {
        return RegionRefDoor::element_at_<Brand>(first, count, position);
    }
};

namespace detail {

// The slots of a FixedList: N elements, each of which starts its lifetime at
// an append and ends it at a take or at the end of the list.  For an element
// that is trivially default constructible, the union value-initializes each
// slot, so no read of a slot past the count can see an indeterminate value.
// For each other element the union starts its empty member and no element,
// so its constructor and its destructor touch no slot.  The empty member is
// the active member of the union, so a list of such an element is a constant
// expression: a static list is constant-initialized, its bytes stay zero,
// and no page of its slots is written before the first append.  An append
// starts an element of the array, and the array is then the active member.
// It is trivially copyable and trivially destructible when its element is,
// and the list then copies its bytes.  For each other element the list
// copies, moves and ends each element below its count itself.  The count of
// the list is the tag of the union: no code reads a slot at or past it.
template <class T, std::size_t N>
union ListSlots {
private:
    // The active member before the first append of an element that is not
    // trivially default constructible.  It holds no value.
    struct NoElement final {};

    NoElement no_element_;
    T elements_[N];
    // The bytes of the array.  No code reads or writes them.  GCC 16 treats a
    // byte of a union as padding when the byte is padding in each member, so
    // with this member the union has no padding byte.  Then a
    // value-initialization, such as `FixedList<T, N> list{}`, writes no slot.
    // Without this member, GCC 16 clears each padding byte of each element of
    // such a list at its own store.
    unsigned char bytes_[sizeof(T) * N];

    // Only the list of the same element and capacity reaches a slot.
    friend class FixedList<T, N>;

    // The slot at position, which is below N.  A storage door: the slot is in
    // the union of the list, which only the list reaches.
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]] constexpr T* slot(std::size_t position) noexcept {
        return &elements_[position];
    }
    [[nodiscard]] constexpr T const* slot(std::size_t position) const noexcept { return &elements_[position]; }

public:
    constexpr ListSlots() noexcept
        requires std::is_trivially_default_constructible_v<T>
        : elements_{} {}
    constexpr ListSlots() noexcept : no_element_{} {}

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

// An element that moves as its bytes and has no end.  Its assignment can be
// its own, so the element is not trivially copyable.
template <class T>
concept ByteMovedListElement = std::is_trivially_move_constructible_v<T> && std::is_trivially_destructible_v<T>;

// An index with no element, and the count of the list that refuses it.
struct ListIndexOutside final {
    std::size_t index = 0;
    std::size_t count = 0;
};

// The fatal exit of a value door whose index has no element.
[[noreturn]] CRUCIBLE_COLD inline void report_list_index_outside_(ListIndexOutside outside) noexcept {
    fatal("a write at the index {} of a FixedList of {} elements", outside.index, outside.count);
}

// The fatal exit of a door of a list that the code of an element changed while
// the door moved or ended an element.
[[noreturn]] CRUCIBLE_COLD inline void report_list_changed_() noexcept {
    fatal("the code of an element changed a FixedList while a door of the list moved or ended its elements");
}

}  // namespace detail

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_move_constructible_v<T>
class[[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
     [[= ::foundation::lifetime::no_start_over_bytes{}]][[= equal_parts{}]] FixedList final {
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

    // True when the move, the end, or one of the move, the end and the
    // assignment of an element can be code of the caller.  A check of the count
    // has no code for an element with none.
    static constexpr bool runs_move_code_ = !std::is_trivially_move_constructible_v<T>;
    static constexpr bool runs_end_code_ = !std::is_trivially_destructible_v<T>;
    static constexpr bool runs_element_code_ =
        runs_move_code_ || runs_end_code_ || !std::is_trivially_move_assignable_v<T>;

    // Ends the process when the code of an element changed the count while a
    // door of the list worked on an element.  RunsCode is true when the door
    // ran code of an element.
    template <bool RunsCode = runs_element_code_>
    constexpr void require_count_(std::size_t count) const noexcept {
        if constexpr (RunsCode) {
            if (count_ != count) [[unlikely]] {
                detail::report_list_changed_();
            }
        }
    }

    // Ends the element in the first slot after the elements that the list
    // counts.  The destructor of the element can reach the list.  An append
    // or an insert from it would start an element in the slot whose
    // destructor still runs, so the process ends when the count grew.
    constexpr void end_dropped_() noexcept {
        std::size_t const count = size();
        end_slot_(*slots_.slot(count));
        if constexpr (runs_end_code_) {
            if (count_ > count) [[unlikely]] {
                detail::report_list_changed_();
            }
        }
    }

    // Ends each element from the last.  The list stops to count an element
    // before the element ends, so the destructor of an element that reaches
    // the list finds a whole list.  Complexity: O(size()).
    constexpr void end_each_() noexcept {
        if constexpr (std::is_trivially_destructible_v<T>) {
            count_ = 0;
        } else {
            while (count_ != 0) {
                --count_;
                end_dropped_();
            }
        }
    }

    // Starts each element of this empty list as a copy of the element at the
    // same position of source.  Complexity: O(source.size()).
    constexpr void copy_each_(FixedList const& source) noexcept {
        std::size_t const count = source.size();
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(slots_.slot(position))) T(*source.slots_.slot(position));
        }
        constexpr bool runs_copy_code = !std::is_trivially_copy_constructible_v<T>;
        require_count_<runs_copy_code>(0);
        source.template require_count_<runs_copy_code>(count);
        count_ = source.count_;
    }

    // Starts each element of this empty list as a move of the element at the
    // same position of source, and ends each element of source.  Complexity:
    // O(source.size()).  A body that moves storage: the move of the list
    // that owns source gives it up.
    [[= ::foundation::linear::moves_own_storage{}]] constexpr void move_each_(FixedList& source) noexcept {
        std::size_t const count = source.size();
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(slots_.slot(position))) T(static_cast<T&&>(*source.slots_.slot(position)));
        }
        require_count_<runs_move_code_>(0);
        source.template require_count_<runs_move_code_>(count);
        count_ = source.count_;
        source.end_each_();
    }

    // True when an element takes a new value by a nothrow assignment.  Then a
    // slot holds an element at each step of a value door.  An assignment over
    // a linear element drops it with no door, so a linear element ends and
    // starts in its slot.
    static constexpr bool is_assigned_in_place_ =
        std::is_nothrow_move_assignable_v<T> && !::foundation::linear::is_linear_type(^^T);

    // Starts value as the element of the free slot.
    constexpr void start_slot_(std::size_t slot, T&& value) noexcept {
        ::new(static_cast<void*>(slots_.slot(slot))) T(static_cast<T&&>(value));
    }

    // The doors of an element with no assignment in place free a slot that the
    // list counts.  hide_tail_ lets the list count only the elements before the
    // slot, so the list is whole while code of the caller runs, and it gives
    // the old count.  show_tail_ restores the count.  It ends the process
    // when the code of an element changed the list, because the slots after
    // the first free one hold elements that the list does not count.
    [[nodiscard]] constexpr std::size_t hide_tail_(std::size_t slot) noexcept {
        std::size_t const count = size();
        count_ = static_cast<detail::IndexWord<N>>(slot);
        return count;
    }
    constexpr void show_tail_(std::size_t slot, std::size_t count) noexcept {
        require_count_(slot);
        count_ = static_cast<detail::IndexWord<N>>(count);
    }

    // Puts value in the place of the element of slot, and gives the old
    // element.  Code of the caller that runs is the move of an element, the
    // assignment of one, and, with no assignment, the destructor of the
    // moved-from one.  The list is whole at each of them.  A body that moves
    // storage: the caller ends the element that it gets.
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]] constexpr T replace_slot_(std::size_t slot,
                                                                                            T&& value) noexcept {
        if constexpr (is_assigned_in_place_) {
            std::size_t const count = size();
            T held(static_cast<T&&>(*slots_.slot(slot)));
            require_count_(count);
            *slots_.slot(slot) = static_cast<T&&>(value);
            require_count_(count);
            return held;
        } else {
            std::size_t const count = hide_tail_(slot);
            T held(static_cast<T&&>(*slots_.slot(slot)));
            end_slot_(*slots_.slot(slot));
            require_count_(slot);
            start_slot_(slot, static_cast<T&&>(value));
            show_tail_(slot, count);
            return held;
        }
    }

    // Puts value before the element of slot and moves each element from slot
    // one place up.  The list has room.  The count takes the new element
    // last.  A body that moves storage, as replace_slot_.
    [[= ::foundation::linear::moves_own_storage{}]] constexpr void open_slot_(std::size_t slot, T&& value) noexcept {
        std::size_t const count = size();
        if (slot == count) {
            start_slot_(slot, static_cast<T&&>(value));
            require_count_(count);
        } else if constexpr (is_assigned_in_place_) {
            start_slot_(count, static_cast<T&&>(*slots_.slot(count - 1)));
            require_count_(count);
            for (std::size_t later = count - 1; later != slot; --later) {
                *slots_.slot(later) = static_cast<T&&>(*slots_.slot(later - 1));
            }
            *slots_.slot(slot) = static_cast<T&&>(value);
            require_count_(count);
        } else {
            static_cast<void>(hide_tail_(slot));
            for (std::size_t later = count; later != slot; --later) {
                start_slot_(later, static_cast<T&&>(*slots_.slot(later - 1)));
                end_slot_(*slots_.slot(later - 1));
                require_count_(slot);
            }
            start_slot_(slot, static_cast<T&&>(value));
            show_tail_(slot, count);
        }
        ++count_;
    }

    // Ends the moved-from element of slot and moves each later element one
    // place down.  The list counts the last element no more before it ends.
    // A body that moves storage, as replace_slot_.
    [[= ::foundation::linear::moves_own_storage{}]] constexpr void close_slot_(std::size_t slot) noexcept {
        std::size_t const count = size();
        if constexpr (is_assigned_in_place_) {
            for (std::size_t later = slot; later + 1 != count; ++later) {
                *slots_.slot(later) = static_cast<T&&>(*slots_.slot(later + 1));
            }
            require_count_(count);
            --count_;
            end_dropped_();
        } else {
            static_cast<void>(hide_tail_(slot));
            end_slot_(*slots_.slot(slot));
            require_count_(slot);
            for (std::size_t later = slot; later + 1 != count; ++later) {
                start_slot_(later, static_cast<T&&>(*slots_.slot(later + 1)));
                end_slot_(*slots_.slot(later + 1));
                require_count_(slot);
            }
            show_tail_(slot, count - 1);
        }
    }

    // The frame of one call of update: the copy of an element, which the
    // destructor assigns to the element at the index.  It names the list and
    // no slot, so a body that ends or moves the elements of the list ends the
    // process at the index, and it writes no ended element.
    class ElementUpdate final {
        FixedList& list_;
        Index<N> index_;
        T held_;
        [[no_unique_address]] detail::LostUpdateOracle<T> oracle_;

    public:
        constexpr ElementUpdate(FixedList& list, Index<N> index) noexcept
            : list_{list},
              index_{index},
              held_(*list.slots_.slot(index.raw())),
              oracle_{*list.slots_.slot(index.raw())} {}
        ElementUpdate(ElementUpdate const&) = delete("the frame of update puts its copy back one time");
        ElementUpdate& operator=(ElementUpdate const&) = delete("the frame of update puts its copy back one time");
        constexpr ~ElementUpdate() {
            list_.require_element_(index_);
            T& element = *list_.slots_.slot(index_.raw());
            oracle_.require_unwritten(element);
            element = static_cast<T&&>(held_);
        }
        // The call of the body with the copy.  It gives the result of the body.
        template <class Body>
        constexpr auto run(Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
            return static_cast<Body&&>(body)(held_);
        }
    };

    // The index of a live element, from its address.  append passes the pointer
    // that its placement new gives.  With an index from the count, or with a
    // pointer read again from the slot, GCC 16 gives a loop of appends a store
    // with an index register (the probe aligned_bytes_two_windows of
    // test/shape/core_shapes.cpp).
    [[nodiscard]] constexpr Index<N> index_of_(T const* element) const noexcept {
        std::size_t const at = static_cast<std::size_t>(element - first_());
        [[assume(at < N)]];
        return Index<N>::check(at).expect("a live element of a list is below its capacity");
    }

    // Ends the process when index has no element (the writers).
    constexpr void require_element_(Index<N> index) const noexcept {
        if (index.raw() >= count_) [[unlikely]] {
            detail::report_list_index_outside_(detail::ListIndexOutside{.index = index.raw(), .count = count_});
        }
    }

    // The refusal of a value by a full list: a linear value goes back to the
    // caller, and each other value ends.
    [[nodiscard]] static constexpr Full<T> refused_(T&& value) noexcept {
        if constexpr (::foundation::linear::is_linear_type(^^T)) {
            return Full<T>{static_cast<T&&>(value)};
        } else {
            return Full<T>{};
        }
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
    // A move of an element that moves as its bytes and has no end, such as a
    // ViewMut, copies the bytes of the slots, and the source is then empty.
    constexpr FixedList(FixedList&& other) noexcept
        requires(!detail::BytewiseListElement<T> && detail::ByteMovedListElement<T>)
        : slots_{static_cast<detail::ListSlots<T, N>&&>(other.slots_)}, count_{other.count_} {
        other.count_ = 0;
    }
    constexpr FixedList(FixedList&& other) noexcept
        requires(!detail::BytewiseListElement<T> && !detail::ByteMovedListElement<T>)
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

    // Puts value after the last element, and gives the index of the element,
    // or Full<T> when the list holds N elements.  One compare.
    [[nodiscard]] constexpr Result<Index<N>, Full<T>> append(T value) & noexcept {
        if (count_ == N) [[unlikely]] {
            return err(refused_(static_cast<T&&>(value)));
        }
        std::size_t const count = size();
        T const* const element = ::new(static_cast<void*>(slots_.slot(count_))) T(static_cast<T&&>(value));
        require_count_<runs_move_code_>(count);
        ++count_;
        return index_of_(element);
    }
    void append(T) && = delete("an append into a temporary FixedList puts a value into an object that the full "
                               "expression ends");

    // Puts value before the element at index, or after the last element when
    // index is size(), and moves each element from index one place up.  A list
    // of N elements gives Full<T>.  An index past size() ends the process.
    // Complexity: O(size() - index) moves.
    [[nodiscard]] constexpr Result<Unit, Full<T>> insert(Index<N> index, T value) & noexcept {
        if (count_ == N) [[unlikely]] {
            return err(refused_(static_cast<T&&>(value)));
        }
        if (index.raw() > count_) [[unlikely]] {
            detail::report_list_index_outside_(detail::ListIndexOutside{.index = index.raw(), .count = count_});
        }
        open_slot_(index.raw(), static_cast<T&&>(value));
        return Unit{};
    }

    // The value doors of one element (the writers): value takes the place of
    // the element at index.  set ends the old element after the slot holds the
    // new one, so the element is one that a holder can drop.  exchange gives
    // the old element back.  An index at or past size() ends the process.
    // Complexity: O(1), and the cost of the moves.
    constexpr void set(Index<N> index, T value) & noexcept
        requires DroppablePayload<T>
    {
        require_element_(index);
        static_cast<void>(replace_slot_(index.raw(), static_cast<T&&>(value)));
    }
    [[nodiscard]] constexpr T exchange(Index<N> index, T value) & noexcept {
        require_element_(index);
        return replace_slot_(index.raw(), static_cast<T&&>(value));
    }

    // The update door of one element (the writers): body gets a copy of the
    // element at index, and then the door puts the copy in the place of the
    // element.  The door gives the result of body, which holds no borrow.  No
    // reference of the door names a slot of the list while body runs.  A
    // nested set or update of the same element loses its write, as in the
    // update door of a FixedArray (foundation/core/Array.h).  An index at or
    // past size() ends the process.  Complexity: O(1), and the cost of body,
    // of one copy and of one move.
    template <class Body>
        requires UpdatableElement<T> && is_assigned_in_place_
              && std::is_invocable_v<Body, T&> && HoldsNoBorrow<detail::body_result_t<Body, T&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        update(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        require_element_(index);
        ElementUpdate frame{*this, index};
        return frame.run(static_cast<Body&&>(body));
    }
    template <class Body>
        requires LinearElementBody<Body, T>
    void update(Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!UpdatableElement<T> || !is_assigned_in_place_)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(Index<N> index, Body&& body) & = delete(
        "update gives the body a copy of the element and assigns the copy back, so the element copies and assigns with "
        "no throw: take an element that does not with exchange");

    // Moves the element at index out and moves each later element one place
    // down, or gives no value at or past size().  Complexity:
    // O(size() - index) moves.
    [[nodiscard]] constexpr Option<T> remove(Index<N> index) & noexcept {
        if (index.raw() >= count_) {
            return Option<T>{none};
        }
        std::size_t const count = size();
        Option<T> result = Option<T>::some(static_cast<T&&>(*slots_.slot(index.raw())));
        require_count_<runs_move_code_>(count);
        close_slot_(index.raw());
        return result;
    }

    // Moves the last element out, or gives no value for an empty list.  One
    // compare.
    [[nodiscard]] constexpr Option<T> take_last() & noexcept {
        if (count_ == 0) {
            return Option<T>{none};
        }
        if constexpr (runs_move_code_ || runs_end_code_) {
            std::size_t const count = size();
            Option<T> result = Option<T>::some(static_cast<T&&>(*slots_.slot(count - 1)));
            require_count_<runs_move_code_>(count);
            --count_;
            end_dropped_();
            return result;
        } else {
            --count_;
            return Option<T>::some(static_cast<T&&>(*slots_.slot(count_)));
        }
    }

    // Gives each element to body by value, from the first to the last, and ends
    // the list.  The list moves into the frame of the door, so no other name
    // has an element while body runs.  Only an element that a holder can drop
    // has drain(): a body that takes the element by reference would drop a
    // linear value.  Complexity: O(size()) moves and calls of body.
    template <class Body>
        requires DroppablePayload<T> && std::is_nothrow_invocable_r_v<void, Body&, T>
              && ::foundation::linear::HoldsNoLinearValue<Body>
    constexpr void drain(Body&& body) && noexcept {
        FixedList held{static_cast<FixedList&&>(*this)};
        for (std::size_t position = 0; position != held.count_; ++position) {
            static_cast<void>(body(static_cast<T&&>(*held.slots_.slot(position))));
        }
    }
    template <class Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&, T>
    void drain(Body&& body) && = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
    void drain(Body&& body) & = delete("drain gives each element away and ends the list: drain std::move(list)");

    // The end of an empty list that the caller gives up: the process ends
    // with the reason when the list still holds an element.  A list of a
    // linear element ends here after the caller gives each element to its
    // consumer, as an empty Option ends at expect_none.  One compare.
    constexpr void expect_empty(Fmt<> why) && noexcept {
        if (count_ != 0) [[unlikely]] {
            fatal(why);
        }
    }

    // Ends each element.  Only an element that a holder can drop has clear():
    // the end of a linear element would lose what it proves.  Complexity:
    // O(size()).
    constexpr void clear() & noexcept
        requires DroppablePayload<T>
    {
        end_each_();
    }

    // The View of the elements, for a read.  It borrows this list, and a
    // change of the list ends what it borrows.  The count is at most N, and
    // the first count slots hold the elements, so the View comes through the
    // door of the base layer, with no check and no test of a null pointer.  A
    // constant evaluation of an empty list gives the empty View, because the
    // union starts no element, and the evaluation forms no pointer into it.
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        if consteval {
            if (count_ == 0) {
                return View<T const>{};
            }
        }
        return FixedListSlotsDoor::live_slots_(first_(), size());
    }
    void view() && = delete("a view of a temporary FixedList points into an object that the full expression ends");
    void view() const&& = delete("a view of a temporary FixedList points into an object that the full expression ends");

    // The run of the elements for the frame door with_mut: a View that writes
    // them.  The key of a frame keeps each caller outside the base out, so
    // only a frame door gives a View that writes the elements of a list.
    [[nodiscard]] friend constexpr View<T> frame_run_(detail::FrameKey const& /*key*/, FixedList& list) noexcept {
        if consteval {
            if (list.count_ == 0) {
                return View<T>{};
            }
        }
        return FixedListSlotsDoor::live_slots_(list.first_(), list.size());
    }

    // The loop `for (T const& element : list)`.
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void
    begin() const&& = delete("a cursor of a temporary FixedList points into an object that the full expression ends");
    void
    end() const&& = delete("a cursor of a temporary FixedList points into an object that the full expression ends");

    // The element at position for a read, as an Option of a Ref with the fresh
    // brand of the call, or no value at or past size(): one compare.  The pack
    // refuses each explicit argument, and the result type holds the brand in
    // undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        if (position >= size()) {
            return Option<Ref<T const, Fresh>>{none};
        }
        return ListRefDoor::element_at_<Fresh>(first_(), size(), position);
    }
    void get(std::size_t) && = delete("an element of a temporary FixedList dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary FixedList dangles at the end of the full expression");

    // The iteration verbs of the View of the elements.
    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void
    reverse() const&& = delete("a loop over a temporary FixedList points into an object that the full expression ends");

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
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary FixedList points into an object that the full expression ends");

    // Two lists are equal when they hold the same count of elements, and each
    // element equals the element at the same position of the other.  They
    // order as the first pair of elements that differ, or as their counts
    // when one is a prefix of the other.  equal and order of the Protocol
    // family compare the elements.
    [[nodiscard]] friend constexpr bool operator==(FixedList const& left, FixedList const& right) noexcept
        requires Equatable<T>
    {
        return ::foundation::core::equal(left.view(), right.view());
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(FixedList const& left,
                                                                    FixedList const& right) noexcept
        requires Orderable<T>
    {
        return ::foundation::core::order(left.view(), right.view());
    }
};

// ── The frame door ─────────────────────────────────────────────────────────

// The frame door of a FixedList (the definition below).  ListFrameKeyDoor
// names it as a friend, so this declaration stands before the door, and the
// default argument of the brand belongs to it.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ::foundation::reflect::IsInstanceOf<Owner, ^^FixedList>
          && FrameBody<Body, typename Owner::element_type, Fresh>
[[nodiscard]] constexpr detail::frame_return_t<
    detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept;

// The door of the frame door of a FixedList to the key of the frame doors
// (the doors of the owners above foundation/core/Frame.h).  Its one member is
// private, and the frame door is its one friend.  The invariant: the frame
// door holds the list by move in its frame, and lends the run of its live
// elements with the scope brand of its call.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ListFrameKeyDoor final : ::foundation::NoObject<ListFrameKeyDoor> {
    template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh>
        requires NamesNoArgument<Refused...> && MovedOwner<Owner>
              && ::foundation::reflect::IsInstanceOf<Owner, ^^FixedList>
              && FrameBody<Body, typename Owner::element_type, Fresh>
    friend constexpr auto ::foundation::core::with_mut(Owner&& owner, Body&& body) noexcept
        -> detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh>>, Owner>;

    // The key of the frame doors, for the run of the list in the frame.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

// The frame door of a FixedList (foundation/core/Frame.h).  The list moves
// into the frame, body gets the ViewMut of its elements, and the door gives
// the list back: alone for a body that gives void, and after the result in a
// Record for each other body.  The pack refuses each explicit argument, so no
// caller names the brand of the frame.  The list holds its elements inline,
// so the door moves size() elements in and out.  Complexity: O(size()), and
// the cost of the body.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ::foundation::reflect::IsInstanceOf<Owner, ^^FixedList>
          && FrameBody<Body, typename Owner::element_type, Fresh>
[[nodiscard]] constexpr detail::frame_return_t<
    detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept {
    Owner held{static_cast<Owner&&>(owner)};
    View<typename Owner::element_type> const run = frame_run_(ListFrameKeyDoor::key_(), held);
    return detail::give_run_back_<Fresh>(ListFrameKeyDoor::key_(), static_cast<Owner&&>(held), run,
                                         static_cast<Body&&>(body));
}

// A body that is no frame callable binds to this overload, the refused twin
// (FrameBody of foundation/core/Region.h), so the refusal gives its reason.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ::foundation::reflect::IsInstanceOf<Owner, ^^FixedList>
              && FrameGates<Body>
void with_mut(Owner&& owner, Body&& body) = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
             && ::foundation::reflect::IsInstanceOf<Owner, ^^FixedList>
             && LinearFrameBody<Body, typename Owner::element_type, Fresh>
void with_mut(Owner&& owner, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A named list stays with the caller while the body runs, so the body could
// read or write it beside the ViewMut through a capture.  This twin refuses
// a named list and a const one, which cannot move into the frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Owner>, ^^FixedList>
void with_mut(Owner&& owner, Body&& body) = delete(
    "with_mut takes the owner into its frame and gives it back: pass std::move(owner), and keep the owner that the "
    "call returns");

}  // namespace foundation::core
