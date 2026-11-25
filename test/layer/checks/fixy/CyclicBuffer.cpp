// The compile-time checks of fixy/CyclicBuffer.h.

#include <fixy/CyclicBuffer.h>

namespace fixy {

namespace detail::cyclic_buffer_self_test {

using CB8 = CyclicBuffer<int, 8>;

static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>)
              == sizeof(FixedArray<std::uint32_t, 8>) + sizeof(Cyclic<std::size_t, 8>)
                     + sizeof(BoundedMonotonic<std::size_t, 8>));
static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>) == 48);
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>) == 144);
static_assert(alignof(CyclicBuffer<std::uint32_t, 8>) == alignof(std::size_t));
// The copy and the destructor are trivial.  The fill count seals the ring
// against the byte routes, so no byte image holds a count above the
// capacity, and the ring is not trivially copyable.
static_assert(std::is_trivially_copy_constructible_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(!std::is_trivially_copyable_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(!std::is_same_v<CyclicBuffer<int, 8>, FixedArray<int, 8>>);
static_assert(CB8::wrapper_kind() == "structural::CyclicBuffer");

[[nodiscard]] consteval bool default_is_empty() noexcept {
    CB8 b{};
    return b.empty() && !b.full() && b.size() == 0 && CB8::capacity == 8;
}
static_assert(default_is_empty());

// The value of the element back places before the newest.
template <std::size_t Back>
[[nodiscard]] consteval int value_back(CB8 const& b) noexcept {
    return b.with_slot(b.slot_back(index<Back>()), [](int const& value) noexcept { return value; });
}

[[nodiscard]] consteval bool push_grows_and_recent_tracks() noexcept {
    CB8 b{};
    b.push(10);
    b.push(20);
    b.push(30);
    return b.size() == 3 && value_back<0>(b) == 30 && value_back<1>(b) == 20 && value_back<2>(b) == 10;
}
static_assert(push_grows_and_recent_tracks());

// An update writes the slot of its element, and it claims none.
[[nodiscard]] consteval bool push_then_update() noexcept {
    CB8 b{};
    b.push(98);
    b.update(b.slot_back(index<0>()), [](int& element) noexcept { ++element; });
    return b.size() == 1 && value_back<0>(b) == 99;
}
static_assert(push_then_update());

// A push_new starts an element from its value.
[[nodiscard]] consteval bool push_new_starts_an_element() noexcept {
    CB8 b{};
    b.push_new(98);
    b.push_new(99);
    return b.size() == 2 && value_back<0>(b) == 99 && value_back<1>(b) == 98;
}
static_assert(push_new_starts_an_element());

// A ring with no key lends no slot that a body writes: not one of a claim, and
// not one of a slot.
template <typename Ring>
concept LendsAClaim = requires(Ring& ring) { ring.with_claim([](typename Ring::value_type&) noexcept {}); };
template <typename Ring>
concept LendsAFreshClaim = requires(Ring& ring) { ring.with_fresh_claim([](typename Ring::value_type&) noexcept {}); };
template <typename Ring>
concept LendsAWrittenSlot =
    requires(Ring& ring) { ring.with_slot(ring.next_slot(), [](typename Ring::value_type&) noexcept {}); };
static_assert(!LendsAClaim<CB8> && !LendsAFreshClaim<CB8> && !LendsAWrittenSlot<CB8>);

[[nodiscard]] consteval bool saturates_and_evicts() noexcept {
    CB8 b{};
    for (int v = 0; v < 12; ++v)
        b.push(v);
    if (b.size() != 8 || !b.full()) return false;
    return value_back<0>(b) == 11 && value_back<7>(b) == 4;
}
static_assert(saturates_and_evicts());

[[nodiscard]] consteval bool full_empty_transitions() noexcept {
    CB8 b{};
    if (!b.empty()) return false;
    for (int v = 0; v < 8; ++v)
        b.push(v);
    return b.full() && b.size() == 8;
}
static_assert(full_empty_transitions());

[[nodiscard]] consteval bool cursor_tracks_absolute_count() noexcept {
    CB8 b{};
    for (int v = 0; v < 5; ++v)
        b.push(v);
    return b.cursor().raw() == 5 && b.cursor().index() == 5;
}
static_assert(cursor_tracks_absolute_count());

// A fresh claim starts a value-initialized element, so an element whose
// default constructor can throw has no fresh claim: a throw between the end
// of the old element and the start of the new one leaves the slot with no
// element.
struct ThrowingDefault {
    ThrowingDefault() noexcept(false) {}
};
struct CheckBrand {};
template <typename Element>
using KeyedRing = CyclicBuffer<Element, 8, CheckBrand>;
template <typename Ring>
concept HasFreshClaim = requires(Ring& ring, ::foundation::core::LoanKey<CheckBrand>& key) {
    ring.with_fresh_claim(key, [](typename Ring::value_type&) noexcept {});
};
static_assert(HasFreshClaim<KeyedRing<int>>);
static_assert(!HasFreshClaim<KeyedRing<ThrowingDefault>>);

// The ring gives no reference to an element out: each door lends one element
// to a body for the time of the call.
template <typename Ring>
concept GivesAReference = requires(Ring& ring) { ring.recent(0); } || requires(Ring& ring) { ring.claim(); };
static_assert(!GivesAReference<CB8>);

}  // namespace detail::cyclic_buffer_self_test

}  // namespace fixy
