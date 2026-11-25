#pragma once

// The append-only list of the Region family: elements that keep their
// addresses until the list ends.
//
//   SegmentList<T>      the elements in the order of the appends, in segments
//                       on the heap.  The caller states no maximum count, and
//                       a list holds no storage before the first append
//
// Operations
//   size()              the count of the elements
//   append(alloc, value)
//                       puts value after the last element
//   append_with(alloc, make)
//                       builds the element from make(position) in place after
//                       the last element, where position is the position of
//                       the new element.  No element moves, so a pinned
//                       element, such as an Atomic, passes
//   get(position)       the element as an Option of a Ref of a const T with
//                       the fresh brand of the call, or no value at or past
//                       size().  One compare, one bit scan and two loads
//   last()              the last element as the same Option of a Ref, or no
//                       value for an empty list
//   walk(), for (x : list)
//                       each element, in the order of the appends
//   segments()          the walk of the segments: each step gives the View of
//                       the elements of one segment, in the same order.  It is
//                       the form for code that takes a contiguous run
//   std::move(list).drain(body)
//                       gives each element to body by value, in the order of
//                       the appends, and ends the list
//   std::move(list).expect_empty(why)
//                       ends an empty list, and ends the process with why when
//                       the list still holds an element.  A list of a linear
//                       element ends here or in drain
// The allocation capability comes first in each append, so each allocation
// shows at its call.  A list has no mint: its public constructor gives the
// empty list, which proves nothing and allocates nothing.
//
// Segments
//   Segment k holds two times the slots of segment k - 1, and segment 0 holds
//   the least power of two of slots whose bytes are at least 256.  An append
//   into a full list allocates the next segment, writes one word of the
//   directory, and moves no element.  So the most expensive append is one
//   allocation, where the growth of a contiguous list also moves each element.
//   The unused slots are at most the count of the elements and the slots of
//   segment 0, as for a list that doubles.
//
//   The directory has 32 segments, so a list holds at most
//   2^32 - 1 times the slots of segment 0.  That is at least one terabyte of
//   elements.  The append after the last slot of the last segment ends the
//   process, as an exhausted memory does in foundation/core/Heap.h.
//
//   The slots of the segments before segment k are the slots of segment k
//   minus the slots of segment 0.  So the position plus the slots of segment 0
//   has its top bit at the bit of the segment, and the bits below are the
//   offset in the segment.  The read has no division and no loop.
//
// Borrows
//   No operation moves an element or ends it before the list ends, and no
//   operation writes an element.  So a Ref and a View of a segment stay
//   correct across each append, and a list needs no lend count.  The end of
//   the list, its drain and its move assignment end each borrow.  A move of
//   the list moves no element, so it ends no Ref and no View of a segment.
//
//   The directory is in the list object.  So a walk, a walk of the segments
//   and a cursor borrow the list object, and a move of the list ends them.
//   They hold the count of their start, so a walk does not see an element
//   that an append puts into the list during the walk.  A walk of a list
//   that moved away ends the process at its next segment.
//
// Mark of the storage
//   A list of elements that hold no borrow states owns_its_storage{} of
//   foundation/core/Owner.h.  So the kind stale-borrow of the rule view-escape
//   reads an append as no end of a borrow, although an append writes pointers
//   of the state.  Each member that is not const keeps each object that a
//   borrow can name, until the end of the list:
//     append, append_with   write slots that hold no element, the tail, the
//                           count, and a word of the directory that names no
//                           segment before
//     a move assignment     ends the elements of its target, and the rule reads
//                           an assignment as the end of each borrow
//     drain                 moves the list into its frame and ends it there, so
//                           the rule reads the end of that list
//     expect_empty          ends a list that holds no element
//
//   A list of elements that hold a borrow, such as a list of Views, states
//   nothing.  The mark also tells that an owner holds no borrow, and then the
//   rule would read a list of Views of a local as a list that a function can
//   give back.  For such a list the rule reads an append as an end of each
//   borrow of an element, so it refuses a correct use and no wrong one.
//
// Threads
//   A read writes no state of the list, so a list is Shareable when its
//   element is (by_arguments{} of foundation/Transfer.h).  The bodies of
//   run_joined of fixy/os/ThreadTasks.h read one list through references to
//   const, while the caller waits for them.  A body of run_beside reads a
//   Sealed of the list (foundation/core/Sealed.h), because the caller that
//   runs beside it can append to the list.
//
// A list owns its segments alone, on the owner core of
// foundation/core/Owner.h.  A move gives the segments to the target and
// leaves the source empty.  The end of a list ends each element from the
// last, and gives each segment back.  A copy would allocate with no
// allocation capability at its call, so a list does not copy.  A list of a
// linear element is linear itself, and it has no move assignment, because the
// assignment drops the elements of the target.
//
// Element code in an append
//   The make of append_with, and the move of an element that moves with code,
//   run while the next slot has no element.  The rule borrow-match of the
//   quarantine plugin refuses a make that changes the list, through the
//   annotation of append_with and the parameter of make.  A function that
//   this code calls can still reach the list.  So the list has no tail in that
//   time.  An append, a move, a drain or the end of the list from such a
//   function finds the null tail and ends the process, before it gives the
//   slot to a second element and before the storage goes.  A read of the list
//   is correct in that time, and it sees the elements before the append.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Walk.h>

#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

template <class T>
    requires OwnedElement<T>
class SegmentList;
template <class T>
class SegmentCursor;

namespace detail {

template <class T>
class SegmentElementStage;
template <class T>
class SegmentRunStage;

}  // namespace detail

// The door of a SegmentList to the door of a Ref of the Region family
// (RegionRefDoor of foundation/core/Region.h).  Its one member is private,
// and SegmentList is its one friend.  The invariant: each position below the
// count of a list names a live element, and no operation moves or ends an
// element before the list ends.  So the list lends an element after it
// compares the position with the count, and the brand is the fresh brand of
// the public call.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class SegmentRefDoor final : ::foundation::NoObject<SegmentRefDoor> {
    template <class T>
        requires OwnedElement<T>
    friend class SegmentList;

    // An element of a list, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return RegionRefDoor::lend_<Brand>(object);
    }
};

namespace detail {

// The segments of a directory.
inline constexpr std::size_t segment_limit_ = 32;

// The least count of bytes of segment 0: four cache lines.
inline constexpr std::size_t first_segment_bytes_ = 256;

// The power of two of the slots of segment 0: the least one whose slots have
// at least first_segment_bytes_ bytes.
template <class T>
inline constexpr std::size_t first_segment_bits_ =
    static_cast<std::size_t>(bit_width((first_segment_bytes_ + sizeof(T) - 1) / sizeof(T) - 1));

template <class T>
inline constexpr std::size_t first_segment_slots_ = std::size_t{1} << first_segment_bits_<T>;

// The slots of a segment.
template <class T>
[[nodiscard]] constexpr std::size_t segment_slots_(std::size_t segment) noexcept {
    return first_segment_slots_<T> << segment;
}

// The slots of the directory: the slots of segment 0, 1 and so on to the last
// segment.
template <class T>
inline constexpr std::size_t directory_slots_ = first_segment_slots_<T> * ((std::size_t{1} << segment_limit_) - 1);

// The segment of a position, and the offset of the position in it.
struct SegmentPlace final {
    std::size_t segment = 0;
    std::size_t offset = 0;
};

// The place of a position that is at most the slots of the directory (the
// segments).  The sum does not wrap, so it has a one bit at or above the bit of
// segment 0, and the scan of its bits needs no test of zero.
template <class T>
[[nodiscard]] CRUCIBLE_INLINE constexpr SegmentPlace segment_place_(std::size_t position) noexcept {
    std::size_t const key = position + first_segment_slots_<T>;
    [[assume(key >= first_segment_slots_<T>)]];
    std::size_t const top = static_cast<std::size_t>(bits_of<std::size_t> - 1 - countl_zero(key));
    return SegmentPlace{top - first_segment_bits_<T>, key ^ (std::size_t{1} << top)};
}

// Some consecutive elements of a list, or some consecutive segments: the
// first one, and the count.
struct SegmentWindow final {
    std::size_t first = 0;
    std::size_t count = 0;

    // The first limit elements of the window, or each one when limit is
    // larger.
    [[nodiscard]] constexpr SegmentWindow limited(std::size_t limit) const noexcept {
        return SegmentWindow{first, limit < count ? limit : count};
    }

    // The elements after the first skip elements, or none when skip is larger.
    [[nodiscard]] constexpr SegmentWindow skipped(std::size_t skip) const noexcept {
        std::size_t const dropped = skip < count ? skip : count;
        return SegmentWindow{first + dropped, count - dropped};
    }
};

// The fatal exits of a list.
[[noreturn]] CRUCIBLE_COLD inline void report_segment_list_full_() noexcept {
    fatal("an append after the last slot of the last segment of a SegmentList");
}

[[noreturn]] CRUCIBLE_COLD inline void report_segment_list_building_() noexcept {
    fatal("an append, a move, a drain or the end of a SegmentList while an append builds an element of it");
}

[[noreturn]] CRUCIBLE_COLD inline void report_segment_list_moved_() noexcept {
    fatal("a walk reached a segment of a SegmentList that moved away during the walk");
}

[[noreturn]] CRUCIBLE_COLD inline void report_segment_cursor_at_end_() noexcept {
    fatal("a read or a step through the end cursor of a loop over a SegmentList");
}

namespace owner {

// The fields of a SegmentList.  The first words of the directory name the
// segments of the list, and each other word is null.  A segment has at least
// one element, unless an append builds the first one.  tail is the next free
// slot of the last segment, and tail_end is the end of that segment.  The two
// are null when the list has no segment, and while code of an element runs in
// an append (element code in an append).
template <class T>
struct SegmentListState {
    using owner_state_mark = OwnerStateMark;

    T* segments[segment_limit_]{};
    T* tail = nullptr;
    T* tail_end = nullptr;
    std::size_t count = 0;

private:
    // The readers of a slot: the list, the cursor of its loop and the stage of
    // its walk.
    template <class U>
        requires OwnedElement<U>
    friend class ::foundation::core::SegmentList;
    friend class ::foundation::core::SegmentCursor<T>;
    friend class ::foundation::core::detail::SegmentElementStage<T>;

    // The element at a position below the count.  Its segment exists, so the
    // address is not null.  The assume reads a local pointer, so the Option of
    // a Ref needs no test of its niche, as window() of a View does.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr T const& element_(SegmentListState const& state,
                                                                     std::size_t position) noexcept {
        SegmentPlace const place = segment_place_<T>(position);
        T const* const element = state.segments[place.segment] + place.offset;
        [[assume(element != nullptr)]];
        return *element;
    }

    // The first slot of a segment that holds an element of a walk.  The list
    // of the walk can have moved away, and its directory is then null.  A
    // storage door: the elements are in the storage that the state owns.
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]]
    CRUCIBLE_INLINE static constexpr T* segment_first_(SegmentListState const& state, std::size_t segment) noexcept {
        T* const first = state.segments[segment];
        if (first == nullptr) [[unlikely]] {
            report_segment_list_moved_();
        }
        return first;
    }

public:
    // Ends the process while an append builds an element: the tail is then
    // null, and the directory is not empty.
    static constexpr void refuse_building_(SegmentListState const& state) noexcept {
        if (state.tail == nullptr && state.segments[0] != nullptr) [[unlikely]] {
            report_segment_list_building_();
        }
    }

    [[nodiscard]] static constexpr SegmentListState emptied(SegmentListState const& state) noexcept {
        refuse_building_(state);
        return SegmentListState{};
    }

    // The door that ends the element in one slot: the end of the list ends
    // each element, and the drain ends each element that it moved out.
    [[= ::foundation::linear::drop_door{}]] static constexpr void end_slot_(T& element) noexcept { element.~T(); }

    // The count of the elements.  The directory has no slot past its last
    // segment, so the optimizer reads that bound of the count in each build: a
    // position below the count then names a segment of the directory.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr std::size_t count_of_(SegmentListState const& state) noexcept {
        std::size_t const count = state.count;
        [[assume(count <= directory_slots_<T>)]];
        return count;
    }

    // Ends each element from the last, and gives each segment back to the
    // allocation door, unpoisoned (the spare storage of Heap.h): an allocator
    // other than the one of AddressSanitizer gives the block out again with
    // its poison.  Complexity: O(count), and O(the segments) for an element
    // whose end has no code.
    static constexpr void release(SegmentListState const& state) noexcept {
        refuse_building_(state);
        std::size_t const count = count_of_(state);
        if (count == 0) {
            return;
        }
        SegmentPlace const last = segment_place_<T>(count - 1);
        std::size_t live = last.offset + 1;
        for (std::size_t open = last.segment + 1; open != 0; --open) {
            T* const first = state.segments[open - 1];
            std::size_t const slots = segment_slots_<T>(open - 1);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (std::size_t offset = live; offset != 0; --offset) {
                    end_slot_(first[offset - 1]);
                }
            }
            live = slots / 2;
            unpoison_slots_(first, slots);
            heap_release_(static_cast<void*>(first), slots_layout_<T>(slots));
        }
    }

    // Gives the run of the window in each segment to body, in order, as the
    // first slot and the count.  body gives Flow::stop to stop.  Complexity:
    // one call of body for each segment of the window.
    template <class Body>
    [[gnu::always_inline]] static constexpr Flow try_each_run_(SegmentListState const& state, SegmentWindow window,
                                                               Body&& body) noexcept {
        SegmentPlace place = segment_place_<T>(window.first);
        std::size_t left = window.count;
        while (left != 0) {
            std::size_t const room = segment_slots_<T>(place.segment) - place.offset;
            std::size_t const run = left < room ? left : room;
            if (body(segment_first_(state, place.segment) + place.offset, run) == Flow::stop) {
                return Flow::stop;
            }
            left -= run;
            ++place.segment;
            place.offset = 0;
        }
        return Flow::go;
    }
};

}  // namespace owner

}  // namespace detail

// The position of a loop over the elements of a list.  It holds the next
// element and the end of its run in one segment, and it goes to the next
// segment when a step reaches that end.  So a cursor that is not at the end
// of its loop always names an element.  A read or a step at the end ends the
// process in each build.
template <class T>
class SegmentCursor final {
    using State = detail::owner::SegmentListState<T>;

    T const* at_ = nullptr;
    T const* run_end_ = nullptr;
    State const* state_ = nullptr;
    std::size_t segment_ = 0;
    // The elements of the loop after the run of the cursor.
    std::size_t left_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class detail::SegmentElementStage<T>;

    // The cursor at the first element of the window, or at the end of an
    // empty window.
    constexpr SegmentCursor(State const& state, detail::SegmentWindow window) noexcept
        : state_{__builtin_addressof(state)} {
        if (window.count == 0) {
            return;
        }
        detail::SegmentPlace const place = detail::segment_place_<T>(window.first);
        segment_ = place.segment;
        enter_run_(place.offset, window.count);
    }

    // Takes the run of at most left elements from the offset in the segment
    // of the cursor.
    constexpr void enter_run_(std::size_t offset, std::size_t left) noexcept {
        std::size_t const room = detail::segment_slots_<T>(segment_) - offset;
        std::size_t const run = left < room ? left : room;
        at_ = State::segment_first_(*state_, segment_) + offset;
        run_end_ = at_ + run;
        left_ = left - run;
    }

public:
    [[nodiscard]] constexpr T const& operator*() const noexcept {
        if (at_ == run_end_) [[unlikely]] {
            detail::report_segment_cursor_at_end_();
        }
        return *at_;
    }

    constexpr SegmentCursor& operator++() noexcept {
        if (at_ == run_end_) [[unlikely]] {
            detail::report_segment_cursor_at_end_();
        }
        ++at_;
        if (at_ == run_end_ && left_ != 0) {
            ++segment_;
            enter_run_(0, left_);
        }
        return *this;
    }

    // The end of one segment can be the address of the first slot of another
    // one, so the compare reads the elements after the run too.
    [[nodiscard]] friend constexpr bool operator==(SegmentCursor const& left, SegmentCursor const& right) noexcept {
        return left.at_ == right.at_ && left.left_ == right.left_;
    }
};

// The position of a loop over the segments of a list.  A read or a step at
// the end ends the process in each build.
template <class T>
class SegmentRunCursor final {
    using State = detail::owner::SegmentListState<T>;

    State const* state_ = nullptr;
    // The elements of the list at the start of the loop.
    std::size_t count_ = 0;
    detail::SegmentWindow segments_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class detail::SegmentRunStage<T>;

    constexpr SegmentRunCursor(State const& state, std::size_t count, detail::SegmentWindow segments) noexcept
        : state_{__builtin_addressof(state)}, count_{count}, segments_{segments} {}

public:
    [[nodiscard]] constexpr View<T const> operator*() const noexcept {
        if (segments_.count == 0) [[unlikely]] {
            detail::report_segment_cursor_at_end_();
        }
        return SegmentList<T>::run_view_(*state_, count_, segments_.first);
    }

    constexpr SegmentRunCursor& operator++() noexcept {
        if (segments_.count == 0) [[unlikely]] {
            detail::report_segment_cursor_at_end_();
        }
        segments_ = segments_.skipped(1);
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(SegmentRunCursor const& left,
                                                   SegmentRunCursor const& right) noexcept {
        return left.segments_.count == right.segments_.count;
    }
};

namespace detail {

// The root stage of the walk of the elements of a list (the stages of
// foundation/core/Walk.h): one counted loop for each segment.  A list gives
// the walk of each of its elements, and only limited_ and skipped_ give a part
// of a walk.
template <class T>
class SegmentElementStage final {
    using State = owner::SegmentListState<T>;

    State const* state_ = nullptr;
    SegmentWindow window_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr SegmentElementStage(State const& state, SegmentWindow window) noexcept
        : state_{__builtin_addressof(state)}, window_{window} {}

public:
    using element_reference = T const&;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit SegmentElementStage(SegmentList<T> const& list CRUCIBLE_LIFETIMEBOUND) noexcept
        : state_{__builtin_addressof(list.state_)}, window_{0, list.size()} {}
    explicit SegmentElementStage(SegmentList<T> const&&) =
        delete("a walk of a temporary SegmentList dangles at the end of the full expression");

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        static_cast<void>(State::try_each_run_(
            *state_, window_, [&sink] [[gnu::always_inline]] (T const* first, std::size_t count) noexcept {
                for (std::size_t offset = 0; offset < count; ++offset) {
                    sink(first[offset]);
                }
                return Flow::go;
            }));
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        return State::try_each_run_(*state_, window_,
                                    [&sink] [[gnu::always_inline]] (T const* first, std::size_t count) noexcept {
                                        T const* const end = first + count;
                                        for (T const* at = first; at != end; ++at) {
                                            if (sink(*at) == Flow::stop) return Flow::stop;
                                        }
                                        return Flow::go;
                                    });
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return window_.count; }

    // The element at position, which the caller proves below size_() (the
    // stages of foundation/core/Walk.h).
    [[nodiscard]] constexpr T const& at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        SegmentPlace const place = segment_place_<T>(window_.first + position);
        return State::segment_first_(*state_, place.segment)[place.offset];
    }

    [[nodiscard]] constexpr SegmentElementStage limited_(std::size_t count) && noexcept {
        return SegmentElementStage{*state_, window_.limited(count)};
    }

    [[nodiscard]] constexpr SegmentElementStage skipped_(std::size_t count) && noexcept {
        return SegmentElementStage{*state_, window_.skipped(count)};
    }

    // The cursors of `for (x : walk)`.  The end cursor is past the last
    // element of the window, with no element left.
    [[nodiscard]] constexpr SegmentCursor<T> begin_() const noexcept { return SegmentCursor<T>{*state_, window_}; }
    [[nodiscard]] constexpr SegmentCursor<T> end_() const noexcept {
        SegmentCursor<T> cursor{*state_, window_.skipped(window_.count == 0 ? 0 : window_.count - 1)};
        cursor.at_ = cursor.run_end_;
        return cursor;
    }
};

// The root stage of the walk of the segments of a list: each element is the
// View of the elements of one segment.  A list gives the walk of each of its
// segments, and only limited_ and skipped_ give a part of a walk.
template <class T>
class SegmentRunStage final {
    using State = owner::SegmentListState<T>;

    State const* state_ = nullptr;
    // The elements of the list at the start of the walk.
    std::size_t count_ = 0;
    SegmentWindow segments_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr SegmentRunStage(State const& state, std::size_t count, SegmentWindow segments) noexcept
        : state_{__builtin_addressof(state)}, count_{count}, segments_{segments} {}

public:
    using element_reference = View<T const>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = false;

    constexpr explicit SegmentRunStage(SegmentList<T> const& list CRUCIBLE_LIFETIMEBOUND) noexcept
        : state_{__builtin_addressof(list.state_)}, count_{list.size()}, segments_{0, list.segment_count_()} {}
    explicit SegmentRunStage(SegmentList<T> const&&) =
        delete("a walk of a temporary SegmentList dangles at the end of the full expression");

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        for (std::size_t step = 0; step != segments_.count; ++step) {
            sink(SegmentList<T>::run_view_(*state_, count_, segments_.first + step));
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        for (std::size_t step = 0; step != segments_.count; ++step) {
            if (sink(SegmentList<T>::run_view_(*state_, count_, segments_.first + step)) == Flow::stop) {
                return Flow::stop;
            }
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept { return segments_.count; }

    // The segment at position, which the caller proves below size_() (the
    // stages of foundation/core/Walk.h).
    [[nodiscard]] constexpr View<T const> at_(StageKey const& /*key*/, std::size_t position) const noexcept {
        return SegmentList<T>::run_view_(*state_, count_, segments_.first + position);
    }

    [[nodiscard]] constexpr SegmentRunStage limited_(std::size_t count) && noexcept {
        return SegmentRunStage{*state_, count_, segments_.limited(count)};
    }

    [[nodiscard]] constexpr SegmentRunStage skipped_(std::size_t count) && noexcept {
        return SegmentRunStage{*state_, count_, segments_.skipped(count)};
    }

    [[nodiscard]] constexpr SegmentRunCursor<T> begin_() const noexcept {
        return SegmentRunCursor<T>{*state_, count_, segments_};
    }
    [[nodiscard]] constexpr SegmentRunCursor<T> end_() const noexcept {
        return SegmentRunCursor<T>{*state_, count_, segments_.skipped(segments_.count)};
    }
};

}  // namespace detail

// An element that append moves into its slot: it moves with no throw, and its
// address is not its identity (foundation/Pinned.h).  An element that is not
// such a one comes through append_with.
template <class T>
concept MovedSegmentElement = std::is_nothrow_move_constructible_v<T> && !std::is_base_of_v<::foundation::Pinned<T>, T>;

// The arguments of append_with: the allocation capability, and a make that
// gives a T for the position of the new element with no throw.  The result is
// a T and not a type that converts to T, so the list builds the element in
// place, with no copy and no move.  make holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h): the list calls it and does not
// consume it.
template <class T, class Marker, class Make>
concept MakesSegmentElement =
    AllocationCapability<Marker> && std::is_nothrow_invocable_v<Make&, std::size_t>
    && std::same_as<std::invoke_result_t<Make&, std::size_t>, T> && ::foundation::linear::HoldsNoLinearValue<Make>;

// The body of a drain: it takes each element by value with no throw, so it
// owns the element, and a linear element has its consumer.  It holds no
// linear value by value.
template <class T, class Body>
concept DrainsSegmentElement = std::is_nothrow_invocable_r_v<void, Body&, T> && TakesPayloadByValue<Body, T>
                            && ::foundation::linear::HoldsNoLinearValue<Body>;

namespace detail {

// True when an element holds no borrow, read as the transfer walk reads it: an
// owner, such as a Box, holds a borrow when its object does, and a pointer and a
// View hold one.  An element that does not move to another thread, and one with
// a part that is incomplete at the list, count as elements with a borrow.  The
// class reads a function and no concept, because a concept keeps the answer that
// it gave for an incomplete part.
template <class T>
[[nodiscard]] consteval bool holds_no_borrow_of_element_() {
    return ::foundation::transfer::holds(^^T, ::foundation::transfer::Property::SendOwned);
}

// The annotation of a list of elements that hold a borrow: it states nothing.
struct holds_borrows_of_elements final {};

// The mark of a list (the mark of the storage).
template <bool HoldsNone>
inline constexpr auto segment_storage_mark_ = holds_borrows_of_elements{};

template <>
inline constexpr auto segment_storage_mark_<true> = owns_its_storage{};

}  // namespace detail

template <class T>
    requires OwnedElement<T>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]][[= ::foundation::transfer::by_arguments{}]]
                   [[= detail::segment_storage_mark_<detail::holds_no_borrow_of_element_<T>()>]] SegmentList final
    : public OwnerCore<detail::owner::SegmentListState<T>, SegmentList<T>> {
    using State = detail::owner::SegmentListState<T>;
    using Core = OwnerCore<State, SegmentList>;

    friend class detail::SegmentElementStage<T>;
    friend class detail::SegmentRunStage<T>;
    friend class SegmentRunCursor<T>;

    // The elements of one segment of the first count elements of a state, as
    // a View.  The caller proves that the segment holds one of them.  The
    // segment holds the rest of count after the segments before it, or its
    // slots when that is less.
    [[nodiscard]] static constexpr View<T const> run_view_(State const& state, std::size_t count,
                                                           std::size_t segment) noexcept {
        std::size_t const slots = detail::segment_slots_<T>(segment);
        std::size_t const before = slots - detail::first_segment_slots_<T>;
        std::size_t const rest = count - before;
        T const* const first = State::segment_first_(state, segment);
        return View<T const>{SegmentList::run_key_(), first, rest < slots ? rest : slots};
    }

    // The count of the segments of the list.
    [[nodiscard]] constexpr std::size_t segment_count_() const noexcept {
        std::size_t const count = size();
        return count == 0 ? 0 : detail::segment_place_<T>(count - 1).segment + 1;
    }

    // Allocates the next segment, and makes it the tail.  The list has no free
    // slot, so count is the first position of that segment.  No slot of the
    // segment holds an element, so each slot is poisoned (the spare storage of
    // Heap.h).  Complexity: one allocation.
    CRUCIBLE_COLD void open_segment_() noexcept {
        State& state = this->state_;
        State::refuse_building_(state);
        std::size_t const segment = detail::segment_place_<T>(state.count).segment;
        if (segment >= detail::segment_limit_) [[unlikely]] {
            detail::report_segment_list_full_();
        }
        std::size_t const slots = detail::segment_slots_<T>(segment);
        T* const first = static_cast<T*>(detail::heap_allocate_(detail::slots_layout_<T>(slots)));
        detail::poison_slots_(first, slots);
        state.segments[segment] = first;
        state.tail = first;
        state.tail_end = first + slots;
    }

    // The slot of the next element, unpoisoned.  One compare, and the next
    // segment when the last one is full.
    [[nodiscard]] CRUCIBLE_INLINE T* free_slot_() noexcept {
        State& state = this->state_;
        if (state.tail == state.tail_end) [[unlikely]] {
            open_segment_();
        }
        T* const slot = state.tail;
        detail::unpoison_slots_(slot, 1);
        return slot;
    }

    // The slot of the next element and the end of its segment, for the time
    // that code of an element runs.
    struct Claim final {
        T* slot = nullptr;
        T* end = nullptr;
    };

    // Takes the next slot, and leaves the list with no tail until settle_
    // (element code in an append).
    [[nodiscard]] CRUCIBLE_INLINE Claim claim_slot_() noexcept {
        State& state = this->state_;
        Claim const claim{free_slot_(), state.tail_end};
        state.tail = nullptr;
        state.tail_end = nullptr;
        return claim;
    }

    // Counts the element in the claimed slot.
    CRUCIBLE_INLINE void settle_(Claim const& claim) noexcept {
        State& state = this->state_;
        state.tail = claim.slot + 1;
        state.tail_end = claim.end;
        ++state.count;
    }

    // Gives each element of a list that its caller gave up to body by value,
    // and ends the list.
    template <class Body>
    [[= ::foundation::linear::drop_door{}]] static void drain_ended_(SegmentList ended, Body& body) noexcept {
        std::size_t left = ended.size();
        for (std::size_t segment = 0; left != 0; ++segment) {
            std::size_t const slots = detail::segment_slots_<T>(segment);
            std::size_t const run = left < slots ? left : slots;
            T* const first = State::segment_first_(ended.state_, segment);
            for (std::size_t offset = 0; offset != run; ++offset) {
                T& element = first[offset];
                static_cast<void>(body(static_cast<T&&>(element)));
            }
            left -= run;
        }
    }

public:
    using element_type = T;

    // The empty list.  It holds no storage.
    constexpr SegmentList() noexcept = default;

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // list.  The assignment ends each element that this list holds after it
    // takes the segments of other, so other can live in one of those elements.
    // The assignment drops the elements, so a list of a linear element has
    // none.
    constexpr SegmentList(SegmentList&&) noexcept = default;
    constexpr SegmentList& operator=(SegmentList&&) noexcept
        requires DroppablePayload<T>
    = default;
    SegmentList& operator=(SegmentList&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the SegmentList holds, and a linear element must not be dropped: "
             "build a new SegmentList instead");

    SegmentList(SegmentList const&) = delete("a copy of a SegmentList allocates, and no allocation capability shows at "
                                             "a copy");
    SegmentList& operator=(SegmentList const&) = delete("a copy of a SegmentList allocates, and no allocation "
                                                        "capability shows at a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return State::count_of_(this->state_); }

    // Puts value after the last element.  One compare, and one allocation
    // when the last segment is full.  No element moves, so each Ref and each
    // View of a segment stays correct.
    template <class Marker>
        requires AllocationCapability<Marker> && MovedSegmentElement<T>
    void append(Marker /*allocation*/, T value) & noexcept {
        if constexpr (std::is_trivially_move_constructible_v<T>) {
            T* const slot = free_slot_();
            ::new(static_cast<void*>(slot)) T(static_cast<T&&>(value));
            this->state_.tail = slot + 1;
            ++this->state_.count;
        } else {
            Claim const claim = claim_slot_();
            ::new(static_cast<void*>(claim.slot)) T(static_cast<T&&>(value));
            settle_(claim);
        }
    }
    template <class Marker>
        requires AllocationCapability<Marker> && (!MovedSegmentElement<T>)
    void append(Marker allocation, T const& value) & =
        delete("append moves the value into its slot, and this element does not move: build it in place with "
               "append_with");

    // Builds the element from make(position) in the slot after the last
    // element.  The result of make is a T, so the element takes it with no
    // copy and no move.  Complexity: one call of make, one compare, and one
    // allocation when the last segment is full.
    template <class Marker, class Make>
        requires MakesSegmentElement<T, Marker, Make>
    [[= ::foundation::linear::borrow_match{}]] void append_with(Marker /*allocation*/, Make&& make) & noexcept {
        Claim const claim = claim_slot_();
        ::new(static_cast<void*>(claim.slot)) T(make(std::size_t{this->state_.count}));
        settle_(claim);
    }
    template <class Marker, class Make>
        requires AllocationCapability<Marker> && std::is_invocable_v<Make&, std::size_t>
                  && (!::foundation::linear::HoldsNoLinearValue<Make>)
    void append_with(Marker allocation, Make&& make) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // The element at position, for a read, as an Option of a Ref with the
    // fresh brand of the call, or no value at or past size().  The pack
    // refuses each explicit argument, and the result type holds the brand in
    // undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        if (position >= size()) [[unlikely]] {
            return Option<Ref<T const, Fresh>>{none};
        }
        return Option<Ref<T const, Fresh>>::some(SegmentRefDoor::lend_<Fresh>(State::element_(this->state_, position)));
    }
    void
    get(std::size_t) && = delete("an element of a temporary SegmentList dangles at the end of the full expression");
    void get(std::size_t) const&& =
        delete("an element of a temporary SegmentList dangles at the end of the full expression");

    // The last element, for a read, in the form of get, or no value for an
    // empty list.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    last() const& noexcept {
        std::size_t const count = size();
        if (count == 0) [[unlikely]] {
            return Option<Ref<T const, Fresh>>{none};
        }
        return Option<Ref<T const, Fresh>>::some(
            SegmentRefDoor::lend_<Fresh>(State::element_(this->state_, count - 1)));
    }
    void last() && = delete("an element of a temporary SegmentList dangles at the end of the full expression");
    void last() const&& = delete("an element of a temporary SegmentList dangles at the end of the full expression");

    // The walk of the elements, in the order of the appends: the head of a
    // chain of foundation/core/Walk.h.  It borrows the list object.
    [[nodiscard]] constexpr Walk<detail::SegmentElementStage<T>> walk() const& noexcept {
        return Walk<detail::SegmentElementStage<T>>{stage_()};
    }
    void walk() && = delete("a walk of a temporary SegmentList dangles at the end of the full expression");
    void walk() const&& = delete("a walk of a temporary SegmentList dangles at the end of the full expression");

    // The walk of the segments, in the order of the appends: each step gives
    // the View of the elements of one segment.  It borrows the list object.  A
    // View that it gives borrows the elements.
    [[nodiscard]] constexpr Walk<detail::SegmentRunStage<T>> segments() const& noexcept {
        return Walk<detail::SegmentRunStage<T>>{detail::SegmentRunStage<T>{*this}};
    }
    void segments() && = delete("a walk of a temporary SegmentList dangles at the end of the full expression");
    void segments() const&& = delete("a walk of a temporary SegmentList dangles at the end of the full expression");

    // The loop `for (T const& element : list)`.
    [[nodiscard]] constexpr SegmentCursor<T> begin() const& noexcept { return stage_().begin_(); }
    [[nodiscard]] constexpr SegmentCursor<T> end() const& noexcept { return stage_().end_(); }
    void begin() && = delete("a cursor of a temporary SegmentList points into storage that the full expression frees");
    void end() && = delete("a cursor of a temporary SegmentList points into storage that the full expression frees");
    void
    begin() const&& = delete("a cursor of a temporary SegmentList points into storage that the full expression frees");
    void
    end() const&& = delete("a cursor of a temporary SegmentList points into storage that the full expression frees");

    // Gives each element to body by value, in the order of the appends, and
    // ends the list.  The list moves into the frame of the door before the
    // first call, so it is empty while body runs, and body can append to it.
    // Complexity: O(size()) moves and calls of body.
    template <class Body>
        requires MovedSegmentElement<T> && DrainsSegmentElement<T, Body>
    void drain(Body&& body) && noexcept {
        drain_ended_(static_cast<SegmentList&&>(*this), body);
    }
    template <class Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&, T>
    void drain(Body&& body) && = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires ::foundation::linear::HoldsNoLinearValue<Body> && std::is_invocable_v<Body&, T>
                  && (!TakesPayloadByValue<Body, T>)
    void drain(Body&& body) && =
        delete("the body of drain takes each element by value, so it names no object that drain ends: write the "
               "parameter as a value");
    template <class Body>
    void drain(Body&& body) & = delete("drain gives each element away and ends the list: drain std::move(list)");

    // The end of an empty list that the caller gives up: the process ends
    // with the reason when the list still holds an element.  A list of a
    // linear element ends here when it holds none.  One compare.
    constexpr void expect_empty(Fmt<> why) && noexcept {
        if (this->state_.count != 0) [[unlikely]] {
            fatal(why);
        }
    }

private:
    // The stage of the walk of each element.
    [[nodiscard]] constexpr detail::SegmentElementStage<T> stage_() const noexcept {
        return detail::SegmentElementStage<T>{*this};
    }
};

}  // namespace foundation::core
