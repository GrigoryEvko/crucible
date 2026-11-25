#pragma once

// The growable text of the Region family, with a maximum that its mint states,
// and the text that it gives after its last append.  A list that grows is a
// SegmentList (foundation/core/SegmentList.h), whose elements do not move, or a
// SmallList (foundation/core/SmallList.h).
//
//   GrowText        the characters on the heap
//   HeapText        the characters of a GrowText after its last append, which
//                   no operation changes
//
// Mint
//   mint_grow_text(maximum)      the one door of a GrowText: an empty text
//                       that holds no storage.  No append takes the text past
//                       maximum characters
// The mint allocates nothing, so it takes no allocation capability.  A maximum
// past the largest count of a View becomes that count.
//
// Operations of a GrowText
//   append(alloc, text)    puts the characters of a TextView, of a
//                       View<char const> or one character after the last one,
//                       and gives Unit, or Full<char> when they do not fit
//                       below the maximum.  A refused append changes nothing
//   append_self(alloc, offset, count)
//                       puts the count characters of the text from offset after
//                       the last one, as append does.  The growth copies them
//                       before it gives the old storage back
//   size() and clear()  as FixedText gives them
//   freeze(std::move(text))
//                       gives the characters to a HeapText with no copy, and
//                       leaves the text empty with its maximum
//   with_view(body)     the frame door: calls body with the TextView of the
//                       characters, and gives the result of body
// The frame door lends the storage of the text to the body for the time of the
// call (the lend count).  It is the only read of the characters, so no borrow of
// a GrowText outlives its frame.
//
// The allocation capability comes first in each append, so each growth shows
// at its call.  A growth doubles the capacity, up to the maximum, and an
// exhausted memory ends the process in the allocation door of
// foundation/core/Heap.h.  The first storage holds one cache line.  The text has
// no reserve and no resize.
//
// A growth moves each character into new storage, so it would end each borrow of
// the text.  So the text gives no TextView and no cursor out of a frame.  A text
// appends a part of itself through append_self, which copies the characters
// before the growth gives the old storage back.
//
// Lend count
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
//   owner writes it to a variable of the caller.  A body that can store its
//   View in a variable that it captures by reference does not compile
//   (StoresNoBorrowOutside of CaptureStore.h).
//
//   A SmallList counts its lends in a LendCount of one byte
//   (foundation/core/LendCount.h), and its doors refuse the same changes.
//
// A HeapText holds the characters that a GrowText held at its freeze, and no
// operation of it changes them.  Its view() and its loop then give borrows
// that only its end or a move assignment of it ends, as the View of a
// HeapArray does.  A name that a table keeps for the life of the table is a
// HeapText.
//
// A text owns its storage alone.  A copy would allocate with no capability at
// its call, so a text does not copy.  A move gives the storage to the target
// and leaves the source empty, with its maximum.
//
// The growth of a text and the arithmetic of a capacity are cold, and their
// bodies are in src/foundation/core/Grow.cpp.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/Array.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Cursor.h>
#include <foundation/core/Full.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail {

// The capacity of the next storage of a growable owner whose elements have
// element_bytes bytes: two times capacity, at least needed and at most
// maximum.  The first storage holds one cache line of elements, and at least
// one.  needed is at most maximum.  Complexity: O(1).
[[nodiscard]] std::size_t next_capacity_(std::size_t capacity, std::size_t needed, std::size_t maximum,
                                         std::size_t element_bytes) noexcept;

// The fatal exits of the lend count.  The calls take no argument, so each
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

// Ends the process while a frame door lends the storage (the lend count).
constexpr void refuse_lent_owner_change_(unsigned char lend_count) noexcept {
    if (lend_count != 0) [[unlikely]] {
        report_lent_owner_change_();
    }
}

// The lend of one frame door, until its destructor (the lend count).  The
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

// The fields of a GrowText: the characters, or null when the capacity is
// zero.  A moved-from text keeps its maximum.  The lend count is mutable,
// because the frame door of a const text lends it too (the lend count).  The
// core empties the state at a move and releases it at a move assignment and at
// the end, so emptied and release refuse a lent text.
struct GrowTextState {
    using owner_state_mark = OwnerStateMark;
    static constexpr bool runs_element_code = false;

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
            unpoison_slots_(state.first, state.capacity);
            heap_release_(static_cast<void*>(state.first), slots_layout_<char>(state.capacity));
        }
    }
};

// The fields of a HeapText: the characters of a GrowText at its freeze, and
// the capacity of their storage, which the release gives back.  The
// characters are null when the capacity is zero.  The HeapText gives only
// const borrows of them.
struct HeapTextState {
    using owner_state_mark = OwnerStateMark;
    static constexpr bool runs_element_code = false;

    char* first = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;

    [[nodiscard]] static constexpr HeapTextState emptied(HeapTextState const& /*state*/) noexcept {
        return HeapTextState{};
    }

    // The characters past the size stay poisoned from the GrowText (the
    // spare storage of Heap.h), so the release unpoisons each one first.
    static constexpr void release(HeapTextState const& state) noexcept {
        if (state.first != nullptr) {
            unpoison_slots_(state.first, state.capacity);
            heap_release_(static_cast<void*>(state.first), slots_layout_<char>(state.capacity));
        }
    }
};

}  // namespace owner

}  // namespace detail

class GrowText;

// The mint is the friend of the text, whose one constructor is private.
[[nodiscard]] constexpr GrowText mint_grow_text(std::size_t maximum) noexcept;

// The door of a HeapText and a GrowText to the raw constructor of a TextView
// (the doors of a text of foundation/core/Text.h).  Its one member is
// private, and the two texts are its friends.  The invariant: the state of
// each text holds the first character and the count of its own storage, and
// a GrowText bounds the count at __PTRDIFF_MAX__.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class GrowTextDoor final : ::foundation::NoObject<GrowTextDoor> {
    friend class HeapText;
    friend class GrowText;

    [[nodiscard]] static constexpr TextView text_(char const* first, std::size_t size) noexcept {
        return TextView{first, size};
    }
};

// The characters that a GrowText held at its freeze.  No operation changes
// them, so a borrow of them ends only at the end or at a move assignment of
// the text.  The one door is freeze(std::move(text)) of a GrowText.
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] HeapText final
    : public OwnerCore<detail::owner::HeapTextState, HeapText> {
    using State = detail::owner::HeapTextState;
    using Core = OwnerCore<State, HeapText>;

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
    // comes through the door of the two texts, with no check.
    [[nodiscard]] constexpr TextView view() const& noexcept { return GrowTextDoor::text_(state_.first, state_.size); }
    TextView
    view() const&& = delete("a view of a temporary HeapText points into storage that the full expression frees");

    // The loop `for (char c : text)` reads each character that the text
    // holds.
    [[nodiscard]] constexpr ViewCursor<char const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<char const> end() const& noexcept { return view().end(); }
    void
    begin() const&& = delete("a cursor of a temporary HeapText points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary HeapText points into storage that the full expression frees");

    // True when the two texts hold the same characters.  Complexity: one
    // compare for each character.
    [[nodiscard]] friend constexpr bool operator==(HeapText const& left, HeapText const& right) noexcept {
        return left.view() == right.view();
    }
};

class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] GrowText final
    : public OwnerCore<detail::owner::GrowTextState, GrowText> {
    using State = detail::owner::GrowTextState;
    using Core = OwnerCore<State, GrowText>;

    // Moves the characters into new storage of the next capacity, puts the
    // count characters at characters after them, and gives the old storage
    // back.  The characters can lie in the old storage, so the copy comes
    // before the release.  The count fits below the maximum and not in the
    // storage.  The body is in src/foundation/core/Grow.cpp.  Complexity:
    // one allocation and O(size() + count) copies.
    void grow_and_put_(char const* characters, std::size_t count) noexcept;

    // Puts the count characters at characters after the last one, or gives
    // Full<char> when they do not fit below the maximum.  Three compares and
    // one copy.  The characters can lie in the storage of this text below its
    // count, as append_self gives them, so the copy is a move of bytes.  The
    // storage past the count is poisoned (the spare storage of Heap.h), so a
    // build with AddressSanitizer reports characters from there.  A lent text
    // ends the process (the lend count).
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
            detail::unpoison_slots_(state_.first + state_.size, count);
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
        return put_(first_of_(detail::BaseLayerRead{}, characters), characters.size());
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

    // The text then holds no character, and it keeps its storage, poisoned
    // (the spare storage of Heap.h).  A lent text ends the process (the lend
    // count).
    constexpr void clear() & noexcept {
        detail::refuse_lent_owner_change_(state_.lend_count);
        state_.size = 0;
        detail::poison_slots_(state_.first, state_.capacity);
    }

    // The frame door of the characters: body gets the TextView of the
    // characters, and the text lends its storage until body returns.  The
    // result of body holds no borrow.
    template <class Body>
        requires std::is_invocable_v<Body, TextView> && HoldsNoBorrow<std::invoke_result_t<Body, TextView>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, TextView>
        with_view(Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, TextView>) {
        [[maybe_unused]] detail::OwnerLend const lend{state_.lend_count};
        return static_cast<Body&&>(body)(GrowTextDoor::text_(state_.first, state_.size));
    }
    template <class Body>
        requires std::is_invocable_v<Body, TextView> && HoldsNoBorrow<std::invoke_result_t<Body, TextView>>
                  && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_view(Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

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
// empty with its maximum.  A lent text ends the process (the lend count).
// Only an rvalue freezes, so the call shows the move.  Complexity: O(1).
[[nodiscard]] constexpr auto freeze(GrowText&& text) noexcept -> HeapText {
    detail::owner::GrowTextState frozen = text.take_state_();
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
