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

[[nodiscard]] consteval bool push_grows_and_recent_tracks() noexcept {
    CB8 b{};
    b.push(10);
    b.push(20);
    b.push(30);
    return b.size() == 3 && b.recent(0) == 30 && b.recent(1) == 20 && b.recent(2) == 10;
}
static_assert(push_grows_and_recent_tracks());

[[nodiscard]] consteval bool claim_then_mutate() noexcept {
    CB8 b{};
    int& slot = b.claim();
    slot = 99;
    return b.size() == 1 && b.recent(0) == 99;
}
static_assert(claim_then_mutate());

[[nodiscard]] consteval bool saturates_and_evicts() noexcept {
    CB8 b{};
    for (int v = 0; v < 12; ++v)
        b.push(v);
    if (b.size() != 8 || !b.full()) return false;
    return b.recent(0) == 11 && b.recent(7) == 4;
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

}  // namespace detail::cyclic_buffer_self_test

}  // namespace fixy
