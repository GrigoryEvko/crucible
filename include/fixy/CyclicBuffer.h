#pragma once

// Bounded ring buffer that keeps the last N elements, evicting the oldest on
// overflow, and reads most recent first.
//
// The fill count is a separate saturating counter rather than
// min(cursor.raw(), N).  The cursor is free-running and wraps at 2^bits, so
// once it laps, `raw < N` becomes true again and a derived size would
// under-report a full ring.
//
// Slots
//   A slot is an Index<N> into the storage.  The next claim writes
//   next_slot(), and slot_back(back) names the slot of the element that the
//   claim before the last back claims wrote: slot_back(index<0>()) is the
//   newest element.  A slot of a position at or past size() holds a stale or
//   default element.  The mask keeps each slot inside the storage, so that
//   bound is a bound of logic and not of safety.
//
// Element
//   Each claim of a full ring ends or replaces the element of its slot, so a
//   ring holds only an element that a holder can drop (DroppablePayload of
//   foundation/core/Choice.h).  A claim of a ring of linear elements would end
//   an older one with no consume.
//
// Doors
//   The ring gives no reference to an element out.  A body borrows one
//   element for the time of a call, and its result holds no borrow:
//     with_slot(slot, body)        the element of a slot of a ring with no key,
//                                  to read
//     with_claim(key, body)        the slot of the next claim, with its old
//                                  element, to write
//     with_fresh_claim(key, body)  the slot of the next claim, with a new
//                                  value-initialized element in it
//     with_slot(key, slot, body)   the element of a slot, to write, or to read
//                                  in a const ring
//   A later claim reuses each slot, so a reference that left a door would name
//   a later element.
//
// Value doors
//   A ring of Unkeyed, the default, is the form of a member owner.  It lends
//   no element that a body writes, because another name of the ring, such as
//   a capture, a helper or a Ref, could claim the same slot and end the
//   element while the body holds it.  It writes through value doors:
//     push(value)         the slot of the next claim becomes value
//     push_new(value)     value takes the place of the element of the slot of
//                         the next claim, also for an element with no
//                         assignment whose move and end are trivial
//     update(slot, body)  body changes a copy of the element of a slot, and
//                         the door then puts the copy in its place (the update
//                         door of a FixedArray, foundation/core/Array.h)
//     pass_slot()         claims a slot and keeps its element
//   No body of a caller runs while one of them writes the storage.
//
// Key
//   A ring of a key brand, from mint_keyed_owner(cyclic_buffer_owner<T, N>)
//   of foundation/core/KeyedMint.h, opens a door that writes only with its
//   LoanKey (the loan key of foundation/core/Frame.h), by move or by a
//   borrow, and it refuses a body that reaches that key.  So no code
//   inside a body opens a second door of the ring that writes: not a nested
//   door, not a helper, and not a Ref of the ring.  Its push and its pass
//   write the claim cursor, so they take the key too.  A read of a keyed ring
//   borrows the key as const and refuses a body that reaches the key, because
//   a claim in the body would end the element that the body reads.

#include <fixy/Core.h>
#include <fixy/Cyclic.h>
#include <fixy/Mutation.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Frame.h>
#include <foundation/core/KeyedMint.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

// The body of a door of a ring of Unkeyed: it takes the element, its result
// holds no borrow, it holds no linear value by value, and it stores no
// borrow of the element outside the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).  A later claim reuses each slot, so a
// borrow that a body kept would name a later element.
template <class Body, class Element>
concept CyclicBody =
    std::is_invocable_v<Body&&, Element&> && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>
    && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of a door of a ring of Unkeyed whose result holds no borrow and that
// holds a linear value by value: the linear twin of the door binds to it.
template <class Body, class Element>
concept CyclicLinearBody = (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, Element&>
                        && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>;

// A body of a door of a ring of Unkeyed whose result holds no borrow, that
// holds no linear value and that stores a borrow of the element outside the
// door: the store twin of the door binds to it.
template <class Body, class Element>
concept CyclicStoringBody = ::foundation::linear::HoldsNoLinearValue<Body> && std::is_invocable_v<Body&&, Element&>
                         && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>
                         && (!::foundation::core::StoresNoBorrowOutside<Body>);

// A body that takes its element to write it, and no const element: the refused
// twin of the slot door of a ring of Unkeyed binds to it.
template <class Body, class Element>
concept CyclicWritingBody = std::is_invocable_v<Body&&, Element&> && !std::is_invocable_v<Body&&, Element const&>;

// A body of a door of a ring whose result holds a borrow: the refused twin of
// the door binds to it.
template <class Body, class Element>
concept CyclicBorrowGivingBody =
    std::is_invocable_v<Body&&, Element&> && !::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>;

// A body of a keyed door of a ring that reaches the key of the ring: the
// refused twin of the door binds to it.
template <class Body, class Element, class KeyBrand>
concept CyclicKeyReachingBody =
    std::is_nothrow_invocable_v<Body&&, Element&> && !::foundation::core::ReachesNoLoanKey<Body, KeyBrand>;

// The body of a keyed fresh claim: an element that starts with no throw, and
// a body of a keyed element door.
template <class Body, class Element, class KeyBrand>
concept CyclicFreshBody =
    std::is_nothrow_default_constructible_v<Element> && ::foundation::core::KeyedElementBody<Body, Element, KeyBrand>;

// The ring (the definition below).  CyclicBufferFrameKeyDoor names it as a
// friend, so this declaration stands before the door.  The definition of the
// ring gives the default argument of its key brand.
template <typename T, std::size_t N, ::foundation::brand::IsBrand KeyBrand>
    requires(N > 0 && (N & (N - 1)) == 0) && ::foundation::core::DroppablePayload<T>
class CyclicBuffer;

}  // namespace fixy

namespace foundation::core {

// The door of a CyclicBuffer to the key of the frame doors (the doors of the
// owners above foundation/core/Frame.h).  Its one member is private, and
// CyclicBuffer of fixy/CyclicBuffer.h is its friend.  The invariant: a ring
// reads the run of the FixedArray of its own storage only to write the slot of
// a door of the ring.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class CyclicBufferFrameKeyDoor final : ::foundation::NoObject<CyclicBufferFrameKeyDoor> {
    template <typename T, std::size_t N, ::foundation::brand::IsBrand KeyBrand>
        requires(N > 0 && (N & (N - 1)) == 0) && ::foundation::core::DroppablePayload<T>
    friend class ::fixy::CyclicBuffer;

    // The key of the frame doors, for the storage of a ring.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy {

template <typename T, std::size_t N, ::foundation::brand::IsBrand KeyBrand = ::foundation::core::Unkeyed>
    requires(N > 0 && (N & (N - 1)) == 0) && ::foundation::core::DroppablePayload<T>
class [[nodiscard]][[= ::foundation::core::loan_key_mark_when<::foundation::core::IsKeyBrand<KeyBrand>>]] CyclicBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type capacity = N;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::CyclicBuffer"; }

private:
    static constexpr bool is_keyed_ = ::foundation::core::IsKeyBrand<KeyBrand>;
    using key_type = ::foundation::core::LoanKey<KeyBrand>;

    // The slot of a masked cursor.  The mask of the cursor keeps it below N,
    // so the optimizer removes the check.
    [[nodiscard]] static constexpr Index<N> prove_slot_(size_type masked) noexcept {
        return Index<N>::check(masked).expect("a masked cursor of a CyclicBuffer is below its capacity");
    }

    FixedArray<T, N> storage_{};
    Cyclic<std::size_t, N> cursor_{};
    BoundedMonotonic<std::size_t, N> count_ = mint_bounded_monotonic<std::size_t, N>(std::size_t{0});

    // The slot at a proven index, for a write.  The ring reaches its own
    // storage through the keyed run of a frame, because a FixedArray gives a
    // View that writes only through a frame.
    [[nodiscard]] constexpr T& slot_at_(Index<N> slot) noexcept {
        return frame_run_(::foundation::core::CyclicBufferFrameKeyDoor::key_(), storage_).at(slot);
    }

    // Claims the slot of the next claim and gives it.  The order is bind at
    // the current index, then advance.  The fill guard is load-bearing, not
    // defensive: bump() does not saturate, and its precondition is that the
    // count is below the bound.
    [[nodiscard]] constexpr Index<N> claim_slot_() noexcept {
        Index<N> const slot = prove_slot_(cursor_.index());
        cursor_.advance();
        if (count_.get() < N) count_.bump();
        return slot;
    }

    // Claims the slot of the next claim and starts a value-initialized element
    // in it.  The claim ends the old element and starts the new one in the
    // same storage, so a ring of elements that refuse an assignment, such as
    // an element with a counter that only goes up, recycles its slots.  Value
    // initialization zeroes an element with no user-provided default
    // constructor first, so each padding byte of such an element is zero.
    //
    // The end and the start of an element can be code that reaches the ring
    // through a second name.  Each read of a keyed ring takes its key, which
    // the door holds, apart from a copy of the ring.  Only a keyed ring of a
    // trivially copyable element copies, and its new element then starts in a
    // local object, whose bytes go into the slot.
    [[nodiscard]] T& fresh_slot_() noexcept {
        T* const slot = __builtin_addressof(slot_at_(claim_slot_()));
        if constexpr (std::is_trivially_copyable_v<T>) {
            T const fresh = T();
            __builtin_memcpy(static_cast<void*>(slot), static_cast<void const*>(__builtin_addressof(fresh)), sizeof(T));
            return *__builtin_launder(slot);
        } else {
            slot->~T();
            return *::new(static_cast<void*>(slot)) T();
        }
    }

    // Runs body on the element, and gives the key back: alone for a body that
    // gives void, and after the result in a Record for each other body.  The
    // door gives up its key here, so the helper takes it as an rvalue.
    template <class Body>
    [[nodiscard]] static constexpr auto give_key_back_(key_type&& key, T& element, Body&& body) noexcept
        -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, T&>, key_type> {
        using Given = std::invoke_result_t<Body&&, T&>;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(element);
            return std::move(key);
        } else {
            return ::foundation::core::detail::frame_return_t<Given, key_type>{static_cast<Body&&>(body)(element),
                                                                               std::move(key)};
        }
    }

public:
    constexpr CyclicBuffer() = default;

    // A copy only reads the elements of the source.  A keyed ring copies and
    // moves only when a copy and a move read the bytes of its elements and write
    // none of them, as a keyed FixedArray moves: a body of a frame of the ring
    // can reach the ring by a name, and a move out of it would end the element
    // that the body holds.  The code of an element that a fresh claim ends could
    // copy the ring by a name, and the copy would read the ended element.  The
    // counter of the fill refuses each assignment of a ring.
    constexpr CyclicBuffer(CyclicBuffer const&)
        requires(!is_keyed_) || std::is_trivially_copyable_v<T>
    = default;
    CyclicBuffer(CyclicBuffer const&)
        requires is_keyed_ && (!std::is_trivially_copyable_v<T>)
    = delete("a copy of a keyed CyclicBuffer runs code of its elements, and a fresh claim of the ring could be open: "
             "read the elements with with_slot(key, slot, body)");
    constexpr CyclicBuffer(CyclicBuffer&&)
        requires(!is_keyed_) || std::is_trivially_copyable_v<T>
    = default;
    CyclicBuffer(CyclicBuffer&&)
        requires is_keyed_ && (!std::is_trivially_copyable_v<T>)
    = delete(
        "a move of a keyed CyclicBuffer writes its elements, and a frame of the ring could be open: build the ring "
        "in its place");
    constexpr CyclicBuffer& operator=(CyclicBuffer const&) = default;
    constexpr CyclicBuffer& operator=(CyclicBuffer&&) = default;
    ~CyclicBuffer() = default;

    [[nodiscard]] constexpr size_type size() const noexcept { return count_.get(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_.get() == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return count_.get() == N; }

    // A copy, so no reference into the ring leaves it.  The cursor is one
    // word and trivially copyable, so the copy costs nothing.
    [[nodiscard]] constexpr Cyclic<std::size_t, N> cursor() const noexcept { return cursor_; }

    // The slot of the next claim, and the slot of the element back places
    // before the newest (the slots).
    [[nodiscard]] constexpr Index<N> next_slot() const noexcept { return prove_slot_(cursor_.index()); }
    [[nodiscard]] constexpr Index<N> slot_back(Index<N> back) const noexcept {
        return prove_slot_(cursor_.index_back(back.raw()));
    }

    // ── The doors of a ring of Unkeyed (the value doors) ───────────────────

    // The read of the element of a slot: body gets it as const, in place.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_keyed_) && CyclicBody<Body, T const>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto with_slot(Index<N> slot, Body&& body) const& noexcept {
        return static_cast<Body&&>(body)(storage_.at(slot));
    }

    constexpr void push(T const& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        requires(!is_keyed_) && std::is_copy_assignable_v<T>
    {
        slot_at_(claim_slot_()) = value;
    }

    constexpr void push(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
        requires(!is_keyed_) && std::is_move_assignable_v<T>
    {
        slot_at_(claim_slot_()) = std::move(value);
    }

    // Claims the slot of the next claim, and puts value in the place of its
    // element.  An element with a nothrow assignment takes it, so the slot holds
    // a live element at each step.  An element that refuses an assignment, such
    // as one with a counter that only goes up, ends, and the new element starts
    // in its place.  The end and the move of such an element are trivial
    // (PutBackElement).  So no code of an element, which could read the ring by
    // a second name, runs while the slot holds no element.  Complexity: O(1).
    constexpr void push_new(T&& value) & noexcept
        requires(!is_keyed_) && ::foundation::core::PutBackElement<T>
    {
        T* const slot = __builtin_addressof(slot_at_(claim_slot_()));
        if constexpr (std::is_nothrow_move_assignable_v<T>) {
            *slot = std::move(value);
        } else {
            slot->~T();
            ::new(static_cast<void*>(slot)) T(std::move(value));
        }
    }

    // Body changes a copy of the element of a slot, and the door then puts the
    // copy in the place of the element and gives the result of body.  No
    // reference of the door names the storage of the ring while body runs.
    // Complexity: O(1), and the cost of body, of one copy and of one move.
    template <class Body>
        requires(!is_keyed_) && ::foundation::core::UpdatableElement<T> && CyclicBody<Body, T>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto update(Index<N> slot, Body&& body) & noexcept {
        return storage_.update(slot, static_cast<Body&&>(body));
    }

    // Claims the slot of the next claim and keeps its element, which then
    // reads as the newest.  The call writes no element.
    constexpr void pass_slot() noexcept
        requires(!is_keyed_)
    {
        static_cast<void>(claim_slot_());
    }

    // ── The doors of a ring of a key brand (the key) ───────────────────────

    // The three doors that write, in the two forms of a keyed door: the key
    // moves into the door and comes back, alone for a body that gives void
    // and after the result in a Record for each other body, or the door
    // borrows the key for the call and gives the result of body.  Complexity:
    // O(1), and the cost of body.
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedElementBody<Body, T, KeyBrand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, T&>, key_type>
    with_claim(key_type&& key, Body&& body) & noexcept {
        return give_key_back_(std::move(key), slot_at_(claim_slot_()), static_cast<Body&&>(body));
    }
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedElementBody<Body, T, KeyBrand>
    constexpr auto with_claim(key_type& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(slot_at_(claim_slot_()));
    }

    template <class Body>
        requires is_keyed_ && CyclicFreshBody<Body, T, KeyBrand>
    [[nodiscard]] ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, T&>, key_type>
    with_fresh_claim(key_type&& key, Body&& body) & noexcept {
        return give_key_back_(std::move(key), fresh_slot_(), static_cast<Body&&>(body));
    }
    template <class Body>
        requires is_keyed_ && CyclicFreshBody<Body, T, KeyBrand>
    auto with_fresh_claim(key_type& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(fresh_slot_());
    }

    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedElementBody<Body, T, KeyBrand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, T&>, key_type>
    with_slot(key_type&& key, Index<N> slot, Body&& body) & noexcept {
        return give_key_back_(std::move(key), slot_at_(slot), static_cast<Body&&>(body));
    }
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedElementBody<Body, T, KeyBrand>
    constexpr auto with_slot(key_type& /*key*/, Index<N> slot, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(slot_at_(slot));
    }

    // The read of the element of a slot of a const ring: body gets it as
    // const, in place.  The door borrows the key as const for the call.  Each
    // writer of the ring takes the key too, so no code of the body, which
    // reaches no key of the brand, claims a slot.  Complexity: O(1), and the
    // cost of body.
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedElementBody<Body, T const, KeyBrand>
    constexpr auto with_slot(key_type const& /*key*/, Index<N> slot, Body&& body) const& noexcept {
        return static_cast<Body&&>(body)(storage_.at(slot));
    }

    constexpr void push(key_type& /*key*/, T const& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        requires is_keyed_ && std::is_copy_assignable_v<T>
    {
        slot_at_(claim_slot_()) = value;
    }

    // The pass of a keyed ring.  The pass writes the claim cursor, so it
    // takes the key as each writer of a keyed ring does, although it writes
    // no element.
    constexpr void pass_slot(key_type& /*key*/) noexcept
        requires is_keyed_
    {
        static_cast<void>(claim_slot_());
    }

    // A door of a ring of Unkeyed that lends an element that a body writes
    // binds to these overloads, so the refusal gives its reason (the value
    // doors).
    template <class Body>
        requires(!is_keyed_) && std::is_invocable_v<Body&&, T&>
    void with_claim(Body&& body) & = delete(
        "a CyclicBuffer with no key lends no slot that a body writes, because another name of the ring could claim the "
        "same slot: call push, push_new or update(slot, body), or make the ring with mint_keyed_owner and call "
        "with_claim(key, body)");
    template <class Body>
        requires(!is_keyed_) && std::is_invocable_v<Body&&, T&>
    void with_fresh_claim(Body&& body) & = delete(
        "a CyclicBuffer with no key lends no slot that a body writes, because another name of the ring could claim the "
        "same slot: call push, push_new or update(slot, body), or make the ring with mint_keyed_owner and call "
        "with_fresh_claim(key, body)");
    template <class Body>
        requires(!is_keyed_) && CyclicWritingBody<Body, T>
    void with_slot(Index<N> slot, Body&& body) & = delete(
        "a CyclicBuffer with no key lends no slot that a body writes, because another name of the ring could claim the "
        "same slot: call push, push_new or update(slot, body), or make the ring with mint_keyed_owner and call "
        "with_slot(key, slot, body)");

    // An update of an element that does not copy, or that can throw at a copy
    // or a move, binds to this overload.  An update of an element with no
    // nothrow assignment whose move or end is code binds to the next one, and
    // a push_new of an element that can throw at a move to the one after it.
    // So each refusal gives its reason.
    template <class Body>
        requires(!is_keyed_) && (!::foundation::core::CopiedOutElement<T>) && std::is_invocable_v<Body&&, T&>
    void update(Index<N> slot, Body&& body) & = delete(
        "update gives the body a copy of the element and moves the copy back, so the element copies and moves with no "
        "throw: start a new element with push_new, or make the ring with mint_keyed_owner and call with_slot(key, "
        "slot, body)");
    template <class Body>
        requires(!is_keyed_) && ::foundation::core::CopiedOutElement<T> && (!::foundation::core::PutBackElement<T>)
                 && std::is_invocable_v<Body&&, T&>
    void update(Index<N> slot, Body&& body) & = delete(
        "update of an element with no nothrow assignment ends the element in its slot and starts the copy there, so "
        "the move and the end of the element run no code: give the element a nothrow assignment, or make the ring with "
        "mint_keyed_owner and call with_slot(key, slot, body)");
    void push_new(T&& value) & noexcept
        requires(!is_keyed_) && (!::foundation::core::PutBackElement<T>)
    = delete("push_new of an element with no nothrow assignment ends the old element and starts the new one in its "
             "slot, so the move and the end of the element run no code: give the element a nothrow assignment, or "
             "make the ring with mint_keyed_owner and call with_fresh_claim(key, body)");

    // A door with no key on a keyed ring binds to these overloads, so the
    // refusal gives its reason.
    template <class Body>
        requires is_keyed_
    void update(Index<N> slot, Body&& body) & = delete(
        "a keyed CyclicBuffer has no update door, because another name of it could write a slot while a body holds it: "
        "call with_slot(key, slot, body)");
    void push_new(T&& value) & noexcept
        requires is_keyed_
    = delete("a keyed CyclicBuffer starts a new element only with its LoanKey: call with_fresh_claim(key, body)");
    void pass_slot() noexcept
        requires is_keyed_
    = delete("a keyed CyclicBuffer moves its claim cursor only with its LoanKey: call pass_slot(key)");
    void push(T const& value) noexcept
        requires is_keyed_
    = delete("a keyed CyclicBuffer writes a slot only with its LoanKey: call push(key, value)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body&&, T&>
    void with_claim(Body&& body) & = delete("a keyed CyclicBuffer lends a slot that a body writes only with its "
                                            "LoanKey: call with_claim(std::move(key), body) or with_claim(key, body)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body&&, T&>
    void with_fresh_claim(Body&& body) & = delete("a keyed CyclicBuffer lends a slot that a body writes only with its "
                                                  "LoanKey: call with_fresh_claim(std::move(key), body) or "
                                                  "with_fresh_claim(key, body)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body&&, T&>
    void with_slot(Index<N> slot, Body&& body) & = delete("a keyed CyclicBuffer lends a slot that a body writes only "
                                                          "with its LoanKey: call with_slot(key, slot, body)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body&&, T const&>
    void with_slot(Index<N> slot, Body&& body) const& = delete(
        "a keyed CyclicBuffer lends a slot that a body reads only with its LoanKey, because a claim in the body would "
        "end the element: call with_slot(key, slot, body)");

    // A body that reaches the key of the ring binds to these overloads, so
    // the refusal gives its reason.
    template <class Body>
        requires is_keyed_ && CyclicKeyReachingBody<Body, T, KeyBrand>
    void with_claim(key_type&& key, Body&& body) & = delete(
        "a body of a keyed door of a CyclicBuffer reaches its LoanKey, so code in the body could open a second door "
        "that writes a slot: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && CyclicKeyReachingBody<Body, T, KeyBrand>
    void with_claim(key_type& key, Body&& body) & = delete(
        "a body of a keyed door of a CyclicBuffer reaches its LoanKey, so code in the body could open a second door "
        "that writes a slot: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && CyclicKeyReachingBody<Body, T, KeyBrand>
    void with_slot(key_type&& key, Index<N> slot, Body&& body) & = delete(
        "a body of a keyed door of a CyclicBuffer reaches its LoanKey, so code in the body could open a second door "
        "that writes a slot: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && CyclicKeyReachingBody<Body, T, KeyBrand>
    void with_slot(key_type& key, Index<N> slot, Body&& body) & = delete(
        "a body of a keyed door of a CyclicBuffer reaches its LoanKey, so code in the body could open a second door "
        "that writes a slot: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && CyclicKeyReachingBody<Body, T const, KeyBrand>
    void with_slot(key_type const& key, Index<N> slot, Body&& body) const& = delete(
        "a body of a keyed door of a CyclicBuffer reaches its LoanKey, so code in the body could open a second door "
        "that writes a slot: capture only what the body reads, and keep the key with the caller");

    // A body that holds a linear value by value binds to these overloads, so
    // the refusal gives its reason.
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T const, KeyBrand>
    void with_slot(key_type const& key, Index<N> slot, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicLinearBody<Body, T const>
    void with_slot(Index<N> slot, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicLinearBody<Body, T>
    void update(Index<N> slot, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // A body that stores a borrow of its element outside the door binds to
    // these overloads, so the refusal gives its reason (CyclicStoringBody).
    template <class Body>
        requires(!is_keyed_) && CyclicStoringBody<Body, T const>
    void with_slot(Index<N> slot, Body&& body) const& = delete(CRUCIBLE_STORING_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicStoringBody<Body, T>
    void update(Index<N> slot, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_claim(key_type&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_claim(key_type& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_fresh_claim(key_type&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_fresh_claim(key_type& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_slot(key_type&& key, Index<N> slot, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && ::foundation::core::KeyedLinearElementBody<Body, T, KeyBrand>
    void with_slot(key_type& key, Index<N> slot, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // A body whose result holds a borrow binds to these overloads, so the
    // refusal gives its reason.
    template <class Body>
        requires(!is_keyed_) && CyclicBorrowGivingBody<Body, T>
    void update(Index<N> slot, Body&& body) & =
        delete("a later claim reuses each slot of a CyclicBuffer, so the result of a door holds no reference and no "
               "address: give a plain value");
    template <class Body>
        requires is_keyed_ && CyclicBorrowGivingBody<Body, T>
    void with_claim(key_type& key, Body&& body) & =
        delete("a later claim reuses each slot of a CyclicBuffer, so the result of a door holds no reference and no "
               "address: give a plain value");

    template <class Body>
    void with_slot(Index<N> slot, Body&& body) && =
        delete("a door of a temporary CyclicBuffer lends an element that the full expression ends");
    template <class Body>
    void with_slot(Index<N> slot, Body&& body) const&& =
        delete("a door of a temporary CyclicBuffer lends an element that the full expression ends");
};

// The maker of a keyed CyclicBuffer of N value-initialized elements, for the
// mint of foundation/core/KeyedMint.h (the key): auto [ring, key] =
// mint_keyed_owner(cyclic_buffer_owner<T, N>).  The maker takes the proof of
// the brand of the mint, so it builds a ring only inside a mint.  Complexity:
// O(N) for the value-initialization.
template <class T, std::size_t N>
    requires(N > 0 && (N & (N - 1)) == 0) && ::foundation::core::DroppablePayload<T>
struct CyclicBufferOwner final {
    template <::foundation::core::IsKeyBrand Brand>
        requires std::is_nothrow_default_constructible_v<T>
    [[nodiscard]] constexpr CyclicBuffer<T, N, Brand>
    operator()(::foundation::core::MintedBrand<Brand> /*proof*/) const noexcept {
        return CyclicBuffer<T, N, Brand>{};
    }
};

template <class T, std::size_t N>
    requires(N > 0 && (N & (N - 1)) == 0) && ::foundation::core::DroppablePayload<T>
inline constexpr CyclicBufferOwner<T, N> cyclic_buffer_owner{};

}  // namespace fixy
