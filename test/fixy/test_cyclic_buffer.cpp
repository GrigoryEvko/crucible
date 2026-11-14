// Sentinel TU for fixy/CyclicBuffer.h.  The ring keeps the last N elements,
// reads the most recent first, and its fill count saturates at N while the
// cursor keeps counting every claim.

#include <fixy/CyclicBuffer.h>

#include <cstddef>
#include <cstdint>

namespace {

using Ring = ::fixy::CyclicBuffer<std::uint64_t, 4>;

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
    if (ring.recent(0) != 30 || ring.recent(1) != 20 || ring.recent(2) != 10) return 2;
    return 0;
}

// Past the capacity the oldest element goes, the size stays at N, and the
// cursor keeps the count of every claim.
[[nodiscard]] int evicts_the_oldest_past_capacity() {
    Ring ring{};
    for (std::uint64_t value = 0; value < 11; ++value)
        ring.push(value);
    if (ring.size() != 4 || !ring.full()) return 1;
    if (ring.recent(0) != 10 || ring.recent(3) != 7) return 2;
    if (ring.cursor().raw() != 11 || ring.cursor().index() != 3) return 3;
    return 0;
}

// A claim hands back the slot to fill, and the write lands in the ring.
[[nodiscard]] int claim_returns_the_slot_to_fill() {
    Ring ring{};
    ring.claim() = 99;
    std::uint64_t& slot = ring.claim();
    slot = 7;
    if (ring.size() != 2 || ring.recent(0) != 7 || ring.recent(1) != 99) return 1;
    return 0;
}

// A const ring reads the same slots.
[[nodiscard]] int const_ring_reads_the_same_slots() {
    Ring ring{};
    ring.push(std::uint64_t{5});
    const Ring& view = ring;
    return view.recent(0) == 5 ? 0 : 1;
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
// value-initialized element in its slot.  The reference of the claim reaches
// the slot that recent(0) reads.
[[nodiscard]] int claim_fresh_starts_a_new_element_in_the_slot() {
    ::fixy::CyclicBuffer<Step, 2> ring{};
    ring.claim().value = 11;
    ring.claim().value = 12;
    const int ends_before = g_step_ends;
    Step& fresh = ring.claim_fresh();
    if (g_step_ends != ends_before + 1) return 1;
    if (fresh.value != 7 || &fresh != &ring.recent(0)) return 2;
    if (ring.recent(1).value != 12 || ring.size() != 2) return 3;
    fresh.value = 13;
    if (ring.recent(0).value != 13) return 4;
    return 0;
}

}  // namespace

int main() {
    if (const int failed = starts_empty(); failed != 0) return 10 + failed;
    if (const int failed = reads_most_recent_first(); failed != 0) return 20 + failed;
    if (const int failed = evicts_the_oldest_past_capacity(); failed != 0) return 30 + failed;
    if (const int failed = claim_returns_the_slot_to_fill(); failed != 0) return 40 + failed;
    if (const int failed = const_ring_reads_the_same_slots(); failed != 0) return 50 + failed;
    if (const int failed = claim_fresh_starts_a_new_element_in_the_slot(); failed != 0) return 60 + failed;
    return 0;
}
