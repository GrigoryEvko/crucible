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
// Doors
//   The ring gives no reference to an element out.  A body borrows one
//   element for the time of a call, and its result holds no borrow:
//     with_claim(body)        the slot of the next claim, with its old element
//     with_fresh_claim(body)  the slot of the next claim, with a new
//                             value-initialized element in it
//     with_slot(slot, body)   the element of a slot, to write or to read
//   A later claim reuses each slot, so a reference that left a door would name
//   a later element.  pass_slot() claims a slot and keeps its element, and push
//   writes the slot of the next claim.  Each of the two writes the claim
//   cursor, so a keyed ring takes its key at each.
//
// Key
//   A ring of a key brand, from mint_keyed_owner(cyclic_buffer_owner<T, N>)
//   of foundation/core/KeyedMint.h, opens a door that writes only with its
//   LoanKey (the loan key of foundation/core/Frame.h), by move or by a
//   borrow, and it refuses a body that reaches that key.  So no code
//   inside a body opens a second door of the ring that writes: not a nested
//   door, not a helper, and not a Ref of the ring.  A ring of Unkeyed, the
//   default, is the form of a member owner, and its doors take no key.  The
//   rule borrow-match reads the body of each such door.

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
// holds no borrow, and it holds no linear value by value.
template <class Body, class Element>
concept CyclicBody =
    std::is_invocable_v<Body&&, Element&> && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>
    && ::foundation::linear::HoldsNoLinearValue<Body>;

// A body of a door of a ring of Unkeyed whose result holds no borrow and that
// holds a linear value by value: the linear twin of the door binds to it.
template <class Body, class Element>
concept CyclicLinearBody = (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, Element&>
                        && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Element&>>;

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
    requires(N > 0 && (N & (N - 1)) == 0)
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
        requires(N > 0 && (N & (N - 1)) == 0)
    friend class ::fixy::CyclicBuffer;

    // The key of the frame doors, for the storage of a ring.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy {

template <typename T, std::size_t N, ::foundation::brand::IsBrand KeyBrand = ::foundation::core::Unkeyed>
    requires(N > 0 && (N & (N - 1)) == 0)
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
    [[nodiscard]] T& fresh_slot_() noexcept {
        T* const slot = __builtin_addressof(slot_at_(claim_slot_()));
        slot->~T();
        return *::new(static_cast<void*>(slot)) T();
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

    constexpr CyclicBuffer(CyclicBuffer const&) = default;
    constexpr CyclicBuffer(CyclicBuffer&&) = default;
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

    // The read of the element of a slot: body gets it as const, in place.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires CyclicBody<Body, T const>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto with_slot(Index<N> slot, Body&& body) const& noexcept {
        return static_cast<Body&&>(body)(storage_.at(slot));
    }

    // ── The doors of a ring of Unkeyed (the key) ───────────────────────────

    // Body gets the slot of the next claim with its old element, which it
    // writes.  Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_keyed_) && CyclicBody<Body, T>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto with_claim(Body&& body) & noexcept {
        return static_cast<Body&&>(body)(slot_at_(claim_slot_()));
    }

    // Body gets the slot of the next claim with a new value-initialized
    // element in it.  Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_keyed_) && std::is_nothrow_default_constructible_v<T> && CyclicBody<Body, T>
    [[= ::foundation::linear::borrow_match{}]] auto with_fresh_claim(Body&& body) & noexcept {
        return static_cast<Body&&>(body)(fresh_slot_());
    }

    // Body gets the element of a slot, which it writes.  Complexity: O(1),
    // and the cost of body.
    template <class Body>
        requires(!is_keyed_) && CyclicBody<Body, T>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto with_slot(Index<N> slot, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(slot_at_(slot));
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

    // A door with no key on a keyed ring binds to these overloads, so the
    // refusal gives its reason.
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

    // A body that holds a linear value by value binds to these overloads, so
    // the refusal gives its reason.
    template <class Body>
        requires CyclicLinearBody<Body, T const>
    void with_slot(Index<N> slot, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicLinearBody<Body, T>
    void with_claim(Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicLinearBody<Body, T>
    void with_fresh_claim(Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && CyclicLinearBody<Body, T>
    void with_slot(Index<N> slot, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
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
    void with_claim(Body&& body) & =
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
    requires(N > 0 && (N & (N - 1)) == 0)
struct CyclicBufferOwner final {
    template <::foundation::core::IsKeyBrand Brand>
        requires std::is_nothrow_default_constructible_v<T>
    [[nodiscard]] constexpr CyclicBuffer<T, N, Brand>
    operator()(::foundation::core::MintedBrand<Brand> /*proof*/) const noexcept {
        return CyclicBuffer<T, N, Brand>{};
    }
};

template <class T, std::size_t N>
    requires(N > 0 && (N & (N - 1)) == 0)
inline constexpr CyclicBufferOwner<T, N> cyclic_buffer_owner{};

}  // namespace fixy
