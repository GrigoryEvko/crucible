#pragma once

// The keyed holders of the heap arrays: a holder of a HeapArray or of a
// MaskedArray that lends one element in place only with the LoanKey of its
// brand (the loan key of foundation/core/Frame.h).
//
//   KeyedHeapArray<T, Brand, Align>     holds a HeapArray<T, Align>
//   KeyedMaskedArray<T, Brand, Align>   holds a MaskedArray<T, Align>
//
// A body of the element door of an array with no key can reach the array
// through a second name, such as a capture, a Ref or a static, and replace or
// end it.  The storage of the element then goes back to the allocator while
// the body holds a reference into it.  A keyed holder lends its element only
// with the key of its brand, and the gate of each keyed door refuses a body
// that reaches a key of the brand (ReachesNoLoanKey of foundation/core/Frame.h).
// Each door that writes the holder takes the key.  So no code in a body opens
// a second door of the holder, and no code in a body replaces or takes its
// storage.
//
// Holder
//   A holder does not copy and does not move, as a FixedHeapArray
//   (foundation/core/FixedHeapArray.h).  A class that holds one as a member
//   holds the key of its brand beside it, from mint_held_key (the held key of
//   foundation/core/KeyedMint.h).  A local holder and its key come from the
//   mint of a keyed owner:
//     auto [holder, key] = mint_keyed_owner(keyed_heap_array_owner<T>, std::move(array))
//     auto [holder, key] = mint_keyed_owner(keyed_masked_array_owner<T>, std::move(table))
//   The holder adds no logic of an array: each door calls the door of the
//   array that it holds.  An array lends a plain element in place only to its
//   holder: the holder is a friend of the array, and its keyed element door
//   calls the private slot door of the array.
//
//   KeyedHeapArray<T, Brand, Align> holder{}
//                       the empty holder
//   KeyedHeapArray<T, Brand, Align> holder{std::move(array)}
//                       the holder takes the storage of the array
//
// Reads, with no key
//   size(), view(), for (x : holder)
//                       as the array gives them: each element is const
//   get(position)       of a KeyedHeapArray: an Option of a Ref of the const
//                       element, with the code of get of the HeapArray: the
//                       holder is a friend of HeapArray, so the read takes no
//                       View on its way
//   at_masked(key)      of a KeyedMaskedArray: the const element at
//                       key & (count - 1), as the MaskedArray gives it
//
// Keyed doors
//   holder.with_element(loan, position, on_some, on_none)
//                       of a KeyedHeapArray: on_some gets the element at a
//                       position below size() in place, as T&, and on_none
//                       runs at or past it
//   holder.with_masked(loan, key, body)
//                       of a KeyedMaskedArray: body gets the element at
//                       key & (count - 1) in place, as T&
//   holder.fill(loan, value)
//                       each element becomes value
//   holder.replace(loan, std::move(fresh))
//                       the holder takes the storage of fresh, and the old
//                       elements end after the holder holds fresh
//   holder.take(std::move(loan))
//                       the holder gives its array back and ends the key, and
//                       it is empty after the call
// The element doors, fill and replace borrow the key for the call.  take
// consumes the key.  The result of a body holds no borrow, so no reference to
// an element leaves its door.
//
// A holder has no element door with no key, and no value door (set,
// exchange or update).  A body of a keyed door can reach the holder through a
// capture, and such a door would let it write the storage that the body holds
// a reference into.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The arms of the keyed element door of a KeyedHeapArray: on_some takes the
// element and on_none takes nothing, with no throw, and the two give one type
// that holds no borrow.  The arms and the element reach no key of the brand,
// hold no linear value by value and store no borrow outside the call.
template <class OnSome, class OnNone, class T, class Brand>
concept KeyedElementArms =
    std::is_nothrow_invocable_v<OnSome&&, T&> && std::is_nothrow_invocable_v<OnNone&&>
    && std::same_as<detail::body_result_t<OnSome&&, T&>, std::invoke_result_t<OnNone&&>>
    && HoldsNoBorrow<std::invoke_result_t<OnNone&&>> && ReachesNoLoanKey<OnSome, Brand>
    && ReachesNoLoanKey<OnNone, Brand> && ReachesNoLoanKey<T, Brand> && ::foundation::linear::HoldsNoLinearValue<OnSome>
    && ::foundation::linear::HoldsNoLinearValue<OnNone> && ArmsStoreNoBorrowOutside<OnSome, OnNone>;

// The parts of a holder are KeyedHolderParts, and the declaration of
// KeyedHeapArray in foundation/core/Array.h gives its default alignment,
// because HeapArray names the holder as a friend.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Align>
    requires KeyedHolderParts<T, Brand, Align>
class [[nodiscard]][[= ::foundation::transfer::by_arguments<0>{}]][[= loan_key_mark{}]] KeyedHeapArray final {
    HeapArray<T, Align> storage_{};

public:
    using element_type = T;

    // The empty holder: no element and no storage.
    constexpr KeyedHeapArray() noexcept = default;

    // The holder of the storage of array, which is empty after the call.
    // Complexity: O(1).
    constexpr explicit KeyedHeapArray(HeapArray<T, Align>&& array) noexcept
        : storage_{static_cast<HeapArray<T, Align>&&>(array)} {}

    KeyedHeapArray(KeyedHeapArray const&) =
        delete("a KeyedHeapArray owns its storage alone: a copy would free the storage two times");
    KeyedHeapArray& operator=(KeyedHeapArray const&) =
        delete("a KeyedHeapArray owns its storage alone: a copy would free the storage two times");
    KeyedHeapArray(KeyedHeapArray&&) = delete(
        "a KeyedHeapArray does not move, so a body of a keyed door finds no second holder of its storage: give the "
        "array away with take(std::move(key))");
    KeyedHeapArray& operator=(KeyedHeapArray&&) = delete(
        "a KeyedHeapArray does not move, so a body of a keyed door finds no second holder of its storage: give the "
        "holder new storage with replace(key, std::move(fresh))");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return storage_.size(); }

    // The View of the const elements, as the HeapArray gives it.  It borrows
    // this holder.
    [[nodiscard]] constexpr View<T const> view() const& noexcept { return storage_.view(); }
    void view() && = delete("a view of a temporary KeyedHeapArray points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary KeyedHeapArray points into storage that the full expression frees");

    // The element at position, for a read, as an Option of a Ref with the
    // fresh brand of the call, or no value at or past the count: the body of
    // get of the held HeapArray, one compare.  The read takes no View, so its
    // code is the code of get of a HeapArray.  The pack refuses each explicit
    // argument, and the result type holds the brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        return storage_.template element_of_<Fresh>(position);
    }
    void get(std::size_t) && = delete("an element of a temporary KeyedHeapArray dangles at the end of the full "
                                      "expression");
    void get(std::size_t) const&& = delete("an element of a temporary KeyedHeapArray dangles at the end of the full "
                                           "expression");

    // The loop `for (T const& element : holder)`.
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return storage_.begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return storage_.end(); }
    void begin() && = delete("a cursor of a temporary KeyedHeapArray points into storage that the full expression "
                             "frees");
    void end() && = delete("a cursor of a temporary KeyedHeapArray points into storage that the full expression frees");
    void begin() const&& = delete("a cursor of a temporary KeyedHeapArray points into storage that the full "
                                  "expression frees");
    void end() const&& = delete("a cursor of a temporary KeyedHeapArray points into storage that the full expression "
                                "frees");

    // The keyed element door: on_some gets the element at position in place,
    // as T&, for the time of the call, and at or past size() on_none runs.  The
    // door borrows the key, so no body of it reaches the key, and the private
    // slot door of the HeapArray runs the arms.  The door always inlines,
    // because it runs in the loops of each op.  Complexity: one compare, and
    // the cost of the arm.
    template <class OnSome, class OnNone>
        requires KeyedElementArms<OnSome, OnNone, T, Brand>
    CRUCIBLE_INLINE constexpr auto with_element(LoanKey<Brand>& /*loan*/, std::size_t position, OnSome&& on_some,
                                                OnNone&& on_none) & noexcept {
        return storage_.lend_slot_(position, static_cast<OnSome&&>(on_some), static_cast<OnNone&&>(on_none));
    }
    template <class OnSome, class OnNone>
        requires std::is_nothrow_invocable_v<OnSome&&, T&> && std::is_nothrow_invocable_v<OnNone&&>
                  && (!ReachesNoLoanKey<OnSome, Brand> || !ReachesNoLoanKey<OnNone, Brand>)
    void with_element(LoanKey<Brand>& loan, std::size_t position, OnSome&& on_some, OnNone&& on_none) & = delete(
        "an arm of a keyed element door reaches a LoanKey of the brand of its holder, so code in the arm could open "
        "a second door of the holder: capture only what the arm reads, and keep the key with the caller");
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome&&, T&>
                  && (!::foundation::linear::HoldsNoLinearValue<OnSome>
                      || !::foundation::linear::HoldsNoLinearValue<OnNone>)
    void with_element(LoanKey<Brand>& loan, std::size_t position, OnSome&& on_some,
                      OnNone&& on_none) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class OnSome, class OnNone>
    void with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) const& =
        delete("a KeyedHeapArray lends an element only with its LoanKey: call with_element(key, position, on_some, "
               "on_none), or read the element through view()");

    // Each element becomes value.  The door moves the array into the frame
    // door of a heap array (foundation/core/Array.h) and back, so the holder
    // holds no storage while an assignment of an element runs.  The key keeps
    // each body of a keyed door out, so no reference into the storage lives
    // while the door writes.  Complexity: O(size()).
    void fill(LoanKey<Brand>& /*loan*/, T const& value) & noexcept
        requires std::is_nothrow_copy_assignable_v<T>
    {
        storage_ = with_mut(static_cast<HeapArray<T, Align>&&>(storage_), [&value](auto run) noexcept {
            for (T& element : run) {
                element = value;
            }
        });
    }

    // The holder takes the storage of fresh, and then the elements that it
    // held end, so fresh can live in one of them.  Complexity: O(1), and the
    // end of the old elements.
    void replace(LoanKey<Brand>& /*loan*/, HeapArray<T, Align>&& fresh) & noexcept {
        storage_ = static_cast<HeapArray<T, Align>&&>(fresh);
    }

    // The array of the holder, which is empty after the call.  The door takes
    // the key by value, so the key ends with the door.  Complexity: O(1).
    [[nodiscard]] HeapArray<T, Align> take(LoanKey<Brand> /*loan*/) & noexcept {
        return static_cast<HeapArray<T, Align>&&>(storage_);
    }

    template <class... Arguments>
    void set(Arguments&&... arguments) & = delete(
        "a KeyedHeapArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_element(key, position, on_some, on_none)");
    template <class... Arguments>
    void exchange(Arguments&&... arguments) & = delete(
        "a KeyedHeapArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_element(key, position, on_some, on_none)");
    template <class... Arguments>
    void update(Arguments&&... arguments) & = delete(
        "a KeyedHeapArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_element(key, position, on_some, on_none)");
};

template <class T, ::foundation::brand::IsBrand Brand, std::size_t Align>
    requires KeyedHolderParts<T, Brand, Align>
class [[nodiscard]][[= ::foundation::transfer::by_arguments<0>{}]][[= loan_key_mark{}]] KeyedMaskedArray final {
    MaskedArray<T, Align> storage_{};

public:
    using element_type = T;

    // The empty holder: no element and no storage.  A read of an element of
    // it ends the process, as at_masked of an empty MaskedArray does.
    constexpr KeyedMaskedArray() noexcept = default;

    // The holder of the storage of table, which is empty after the call.
    // Complexity: O(1).
    constexpr explicit KeyedMaskedArray(MaskedArray<T, Align>&& table) noexcept
        : storage_{static_cast<MaskedArray<T, Align>&&>(table)} {}

    KeyedMaskedArray(KeyedMaskedArray const&) =
        delete("a KeyedMaskedArray owns its storage alone: a copy would free the storage two times");
    KeyedMaskedArray& operator=(KeyedMaskedArray const&) =
        delete("a KeyedMaskedArray owns its storage alone: a copy would free the storage two times");
    KeyedMaskedArray(KeyedMaskedArray&&) = delete(
        "a KeyedMaskedArray does not move, so a body of a keyed door finds no second holder of its storage: give the "
        "array away with take(std::move(key))");
    KeyedMaskedArray& operator=(KeyedMaskedArray&&) = delete(
        "a KeyedMaskedArray does not move, so a body of a keyed door finds no second holder of its storage: give the "
        "holder new storage with replace(key, std::move(fresh))");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return storage_.size(); }

    // The View of the const elements, as the MaskedArray gives it.  It
    // borrows this holder.
    [[nodiscard]] constexpr View<T const> view() const& noexcept { return storage_.view(); }
    void
    view() && = delete("a view of a temporary KeyedMaskedArray points into storage that the full expression frees");
    void view() const&& = delete("a view of a temporary KeyedMaskedArray points into storage that the full "
                                 "expression frees");

    // The const element at key & (count - 1), as at_masked of the MaskedArray
    // gives it.  Complexity: O(1).
    [[nodiscard]] constexpr T const& at_masked(std::uint64_t key) const& noexcept { return storage_.at_masked(key); }
    void at_masked(std::uint64_t) && = delete("an element of a temporary KeyedMaskedArray dangles at the end of the "
                                              "full expression");
    void at_masked(std::uint64_t) const&& = delete("an element of a temporary KeyedMaskedArray dangles at the end of "
                                                   "the full expression");

    // The keyed element door: body gets the element at key & (count - 1) in
    // place, as T&, for the time of the call, and the door gives the result
    // of body.  The door borrows the loan key, so no body of it reaches the
    // key, and the private slot door of the MaskedArray runs the body.  The
    // door always inlines, because it runs in the loops of each op.  An empty
    // holder ends the process, as at_masked does.  Complexity: O(1), and the
    // cost of body.
    template <class Body>
        requires KeyedElementBody<Body, T, Brand>
    CRUCIBLE_INLINE constexpr auto with_masked(LoanKey<Brand>& /*loan*/, std::uint64_t key, Body&& body) & noexcept {
        return storage_.lend_masked_slot_(key, static_cast<Body&&>(body));
    }
    template <class Body>
        requires std::is_nothrow_invocable_v<Body&&, T&> && (!ReachesNoLoanKey<Body, Brand>)
    void with_masked(LoanKey<Brand>& loan, std::uint64_t key, Body&& body) & = delete(
        "a body of a keyed element door reaches a LoanKey of the brand of its holder, so code in the body could open "
        "a second door of the holder: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires KeyedLinearElementBody<Body, T, Brand>
    void with_masked(LoanKey<Brand>& loan, std::uint64_t key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
    void with_masked(std::uint64_t key, Body&& body) const& = delete(
        "a KeyedMaskedArray lends an element only with its LoanKey: call with_masked(loan, key, body), or read the "
        "element with at_masked(key)");

    // Each element becomes value, through the frame door of a heap array, as
    // fill of a KeyedHeapArray.  Complexity: O(size()).
    void fill(LoanKey<Brand>& /*loan*/, T const& value) & noexcept
        requires std::is_nothrow_copy_assignable_v<T>
    {
        storage_ = with_mut(static_cast<MaskedArray<T, Align>&&>(storage_), [&value](auto run) noexcept {
            for (T& element : run) {
                element = value;
            }
        });
    }

    // The holder takes the storage of fresh, and then the elements that it
    // held end, as replace of a KeyedHeapArray.  Complexity: O(1), and the
    // end of the old elements.
    void replace(LoanKey<Brand>& /*loan*/, MaskedArray<T, Align>&& fresh) & noexcept {
        storage_ = static_cast<MaskedArray<T, Align>&&>(fresh);
    }

    // The array of the holder, which is empty after the call.  The door takes
    // the key by value, so the key ends with the door.  Complexity: O(1).
    [[nodiscard]] MaskedArray<T, Align> take(LoanKey<Brand> /*loan*/) & noexcept {
        return static_cast<MaskedArray<T, Align>&&>(storage_);
    }

    template <class... Arguments>
    void set_masked(Arguments&&... arguments) & = delete(
        "a KeyedMaskedArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_masked(loan, key, body)");
    template <class... Arguments>
    void exchange_masked(Arguments&&... arguments) & = delete(
        "a KeyedMaskedArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_masked(loan, key, body)");
    template <class... Arguments>
    void update_masked(Arguments&&... arguments) & = delete(
        "a KeyedMaskedArray has no value door, because a body of a keyed door could call it through a second name of "
        "the holder: call with_masked(loan, key, body)");
};

// The maker of a KeyedHeapArray for the mint of foundation/core/KeyedMint.h:
// auto [holder, key] = mint_keyed_owner(keyed_heap_array_owner<T>, std::move(array)).
// The maker takes the proof of the brand of the mint, so it builds a holder of
// a fresh brand only inside a mint.  Complexity: O(1).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && DroppablePayload<T>
struct KeyedHeapArrayOwner final {
    template <IsKeyBrand Brand>
    [[nodiscard]] KeyedHeapArray<T, Brand, Align> operator()(MintedBrand<Brand> /*proof*/,
                                                             HeapArray<T, Align>&& array) const noexcept {
        return KeyedHeapArray<T, Brand, Align>{static_cast<HeapArray<T, Align>&&>(array)};
    }
};

template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && DroppablePayload<T>
inline constexpr KeyedHeapArrayOwner<T, Align> keyed_heap_array_owner{};

// The maker of a KeyedMaskedArray for the mint of foundation/core/KeyedMint.h,
// as the maker of a KeyedHeapArray.  Complexity: O(1).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && DroppablePayload<T>
struct KeyedMaskedArrayOwner final {
    template <IsKeyBrand Brand>
    [[nodiscard]] KeyedMaskedArray<T, Brand, Align> operator()(MintedBrand<Brand> /*proof*/,
                                                               MaskedArray<T, Align>&& table) const noexcept {
        return KeyedMaskedArray<T, Brand, Align>{static_cast<MaskedArray<T, Align>&&>(table)};
    }
};

template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && DroppablePayload<T>
inline constexpr KeyedMaskedArrayOwner<T, Align> keyed_masked_array_owner{};

}  // namespace foundation::core
