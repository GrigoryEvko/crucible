// Sentinel TU for fixy/CyclicBuffer.h.  The ring keeps the last N elements,
// reads the most recent first, and its fill count saturates at N while the
// cursor keeps counting every claim.  A body borrows one element through a
// door, and a keyed ring opens a door that writes only with its key.

#include <fixy/CyclicBuffer.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using Ring = ::fixy::CyclicBuffer<std::uint64_t, 4>;

// The value of the element back places before the newest.
template <class RingType>
[[nodiscard]] std::uint64_t value_back(RingType const& ring, ::fixy::Index<4> back) noexcept {
    return ring.with_slot(ring.slot_back(back), [](std::uint64_t const& value) noexcept { return value; });
}

[[nodiscard]] int starts_empty() {
    const Ring ring{};
    if (!ring.empty() || ring.full() || ring.size() != 0 || Ring::capacity != 4) return 1;
    return 0;
}

// Each push lands at the front, and the ring reads back in reverse order.
[[nodiscard]] int reads_most_recent_first() {
    Ring ring{};
    ring.push(std::uint64_t{10});
    ring.push(std::uint64_t{20});
    ring.push(std::uint64_t{30});
    if (ring.size() != 3 || ring.full()) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 30 || value_back(ring, ::fixy::index<1>()) != 20
        || value_back(ring, ::fixy::index<2>()) != 10) {
        return 2;
    }
    return 0;
}

// Past the capacity the oldest element goes, the size stays at N, and the
// cursor keeps the count of every claim.
[[nodiscard]] int evicts_the_oldest_past_capacity() {
    Ring ring{};
    for (std::uint64_t value = 0; value < 11; ++value)
        ring.push(value);
    if (ring.size() != 4 || !ring.full()) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 10 || value_back(ring, ::fixy::index<3>()) != 7) return 2;
    if (ring.cursor().raw() != 11 || ring.cursor().index() != 3) return 3;
    if (ring.next_slot() != ::fixy::Index<4>{::fixy::index<3>()}) return 4;
    return 0;
}

// A claim lends the slot to fill to a body, and the write lands in the ring.
// A pass claims a slot and keeps its element.
[[nodiscard]] int claim_lends_the_slot_to_fill() {
    Ring ring{};
    ring.with_claim([](std::uint64_t& slot) noexcept { slot = 99; });
    ring.with_claim([](std::uint64_t& slot) noexcept { slot = 7; });
    if (ring.size() != 2 || value_back(ring, ::fixy::index<0>()) != 7 || value_back(ring, ::fixy::index<1>()) != 99) {
        return 1;
    }
    ring.pass_slot();
    if (ring.size() != 3 || value_back(ring, ::fixy::index<0>()) != 0) return 2;
    return 0;
}

// A const ring reads the same slots.
[[nodiscard]] int const_ring_reads_the_same_slots() {
    Ring ring{};
    ring.push(std::uint64_t{5});
    const Ring& view = ring;
    return value_back(view, ::fixy::index<0>()) == 5 ? 0 : 1;
}

// The count of the ends of each Step.  A test reads it around a fresh claim.
int g_step_ends = 0;

// An element whose value only goes up.  The assignment is deleted, as the
// assignment of a counter that refuses a write backward is, so only a fresh
// claim gives its slot a new element.
struct Step {
    std::uint64_t value = 7;

    Step() noexcept = default;
    Step(const Step&) = default;
    Step& operator=(const Step&) = delete("a step only goes up");
    ~Step() { ++g_step_ends; }
};

// A fresh claim of a full ring ends the oldest element one time and starts a
// value-initialized element in its slot, which the slot of the newest names.
[[nodiscard]] int fresh_claim_starts_a_new_element_in_the_slot() {
    ::fixy::CyclicBuffer<Step, 2> ring{};
    ring.with_claim([](Step& step) noexcept { step.value = 11; });
    ring.with_claim([](Step& step) noexcept { step.value = 12; });
    const int ends_before = g_step_ends;
    const std::uint64_t fresh = ring.with_fresh_claim([](Step& step) noexcept {
        std::uint64_t const started = step.value;
        step.value = 13;
        return started;
    });
    if (g_step_ends != ends_before + 1) return 1;
    if (fresh != 7) return 2;
    auto const read = [](Step const& step) noexcept { return step.value; };
    if (ring.with_slot(ring.slot_back(::fixy::index<0>()), read) != 13) return 3;
    if (ring.with_slot(ring.slot_back(::fixy::index<1>()), read) != 12 || ring.size() != 2) return 4;
    return 0;
}

// A keyed ring takes its key at each door that writes.  The move form gives
// the key back, and the borrow form keeps it with the caller.
[[nodiscard]] int keyed_ring_writes_with_its_key() {
    auto [ring, key] = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<std::uint64_t, 4>);
    auto back = ring.with_claim(std::move(key), [](std::uint64_t& slot) noexcept { slot = 41; });
    ring.with_claim(back, [](std::uint64_t& slot) noexcept { slot = 42; });
    ring.push(back, std::uint64_t{43});
    auto swapped =
        ring.with_slot(std::move(back), ring.slot_back(::fixy::index<0>()), [](std::uint64_t& slot) noexcept {
            std::uint64_t const held = slot;
            slot = 44;
            return held;
        });
    std::uint64_t const old_value = swapped.at(::fixy::index<0>());
    if (old_value != 43) return 1;
    if (value_back(ring, ::fixy::index<0>()) != 44) return 2;
    if (value_back(ring, ::fixy::index<2>()) != 41) return 3;
    // The pass of a keyed ring takes the key too, and it keeps the element of
    // the slot that it claims.
    auto& pass_key = swapped.at(::fixy::index<1>());
    ring.pass_slot(pass_key);
    if (ring.size() != 4 || value_back(ring, ::fixy::index<0>()) != 0 || value_back(ring, ::fixy::index<1>()) != 44) {
        return 4;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int failed = starts_empty(); failed != 0) return 10 + failed;
    if (const int failed = reads_most_recent_first(); failed != 0) return 20 + failed;
    if (const int failed = evicts_the_oldest_past_capacity(); failed != 0) return 30 + failed;
    if (const int failed = claim_lends_the_slot_to_fill(); failed != 0) return 40 + failed;
    if (const int failed = const_ring_reads_the_same_slots(); failed != 0) return 50 + failed;
    if (const int failed = fresh_claim_starts_a_new_element_in_the_slot(); failed != 0) return 60 + failed;
    if (const int failed = keyed_ring_writes_with_its_key(); failed != 0) return 70 + failed;
    return 0;
}
